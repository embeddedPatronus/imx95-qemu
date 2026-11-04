/*
 * NXP i.MX95 ele mu
 *
 * EdgeLock Enclave message unit. Answers the ELE requests that U-Boot, OP-
 * TEE and Linux send during boot (enable aux, release RDC, get info,
 * firmware version, TRNG and V2X state, get random) with success responses
 * and raises the receive interrupt.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/guest-random.h"
#include "system/address-spaces.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "system/memory.h"
#include "hw/misc/imx95_ele_mu.h"

#include "hw/misc/imx95_mu_regs.h"


typedef struct Imx95EleMuState Imx95EleMuState;



static uint32_t imx95_ele_mu_status(Imx95EleMuState *s,
                                   unsigned int block,
                                   hwaddr reg_offset)
{
    hwaddr offset = block * IMX95_MU_BLOCK_SIZE + reg_offset;
    uint32_t value = s->regs[offset / sizeof(uint32_t)];

    switch (reg_offset) {
    case IMX95_MU_TSR_OFFSET:
        value |= IMX95_MU_TX_READY_BITS;
        break;
    case IMX95_MU_RSR_OFFSET:
        if (block == s->active_block && s->rx_pos < s->rx_count) {
            value |= IMX95_MU_RX_FULL_BITS;
        }
        break;
    default:
        break;
    }

    return value;
}

static void imx95_ele_mu_update_mu3_irq(Imx95EleMuState *s)
{
    hwaddr block_base = IMX95_ELE_MU3_BLOCK * IMX95_MU_BLOCK_SIZE;
    uint32_t tcr;
    uint32_t rcr;
    uint32_t tsr;
    uint32_t rsr;
    bool mu_source_asserted;
    bool ca55_masked;
    bool level;

    tcr = s->regs[(block_base + IMX95_MU_TCR_OFFSET) /
                  sizeof(uint32_t)];
    rcr = s->regs[(block_base + IMX95_MU_RCR_OFFSET) /
                  sizeof(uint32_t)];
    tsr = imx95_ele_mu_status(s, IMX95_ELE_MU3_BLOCK,
                             IMX95_MU_TSR_OFFSET);
    rsr = imx95_ele_mu_status(s, IMX95_ELE_MU3_BLOCK,
                             IMX95_MU_RSR_OFFSET);

    mu_source_asserted =
        ((tcr & tsr & IMX95_MU_TX_READY_BITS) != 0U) ||
        ((rcr & rsr & IMX95_MU_RX_FULL_BITS) != 0U);
    ca55_masked = s->ca55_masked;
    level = mu_source_asserted && !ca55_masked;

    if (level != s->mu3_irq_level) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.ele-mu3: IRQ level %u -> %u "
                      "tcr=0x%08x tsr=0x%08x "
                      "rcr=0x%08x rsr=0x%08x "
                      "masked=%u active-block=%u "
                      "rx-pos=%u rx-count=%u\n",
                      s->mu3_irq_level, level,
                      tcr, tsr, rcr, rsr,
                      s->ca55_masked,
                      s->active_block,
                      s->rx_pos, s->rx_count);
        s->mu3_irq_level = level;
    }

    qemu_set_irq(s->mu3_irq, level);
}


static void imx95_ele_mu_queue_response(Imx95EleMuState *s)
{
    uint32_t hdr = s->tx_words[0];
    uint8_t ver = hdr & 0xff;
    uint8_t size = (hdr >> 8) & 0xff;
    uint8_t cmd = (hdr >> 16) & 0xff;
    uint8_t tag = (hdr >> 24) & 0xff;

    qemu_log_mask(LOG_GUEST_ERROR,
                  "imx95.ele-mu: ELE req hdr=0x%08x ver=0x%02x "
                  "size=%u cmd=0x%02x tag=0x%02x\n",
                  hdr, ver, size, cmd, tag);

    s->rx_count = 0;
    s->rx_pos = 0;

    switch (cmd) {
    case ELE_ENABLE_AUX_REQ:
        s->rx_words[0] = ((uint32_t) ELE_MSG_TAG_RESP << 24) |
                         ((uint32_t) cmd << 16) |
                         (2U << 8) |
                         ELE_MSG_VER;

        s->rx_words[1] = ELE_SUCCESS_IND;
        s->rx_count = 2;

        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.ele-mu: queued ELE_ENABLE_AUX success\n");
        break;

    case ELE_RELEASE_RDC_REQ:
    {
    s->rx_words[0] = ((uint32_t)ELE_MSG_TAG_RESP << 24) |
                     ((uint32_t)cmd << 16) |
                     (2U << 8) |
                     ELE_MSG_VER;

    s->rx_words[1] = ELE_SUCCESS_IND;
    s->rx_count = 2;

    qemu_log_mask(LOG_GUEST_ERROR,
                  "imx95.ele-mu: queued ELE_RELEASE_RDC success\n");
    }
    break;

    case ELE_GET_INFO_REQ:
    {
        uint64_t info_addr = ((uint64_t)s->tx_words[1] << 32) |
                              (uint64_t)s->tx_words[2];
        /* ele_get_info_data: 160 bytes, zero-filled then set key fields */
        uint8_t info_buf[160];
        memset(info_buf, 0, sizeof(info_buf));
        /* hdr at offset 0 - leave as 0 */
        /* soc at offset 4 */
        info_buf[4]  = (ELE_GET_INFO_SOC_IMX95_REV10 >> 0)  & 0xff;
        info_buf[5]  = (ELE_GET_INFO_SOC_IMX95_REV10 >> 8)  & 0xff;
        info_buf[6]  = (ELE_GET_INFO_SOC_IMX95_REV10 >> 16) & 0xff;
        info_buf[7]  = (ELE_GET_INFO_SOC_IMX95_REV10 >> 24) & 0xff;
        /* lc at offset 8 */
        info_buf[8]  = (ELE_GET_INFO_LC_OEM_OPEN >> 0)  & 0xff;
        info_buf[9]  = (ELE_GET_INFO_LC_OEM_OPEN >> 8)  & 0xff;
        info_buf[10] = (ELE_GET_INFO_LC_OEM_OPEN >> 16) & 0xff;
        info_buf[11] = (ELE_GET_INFO_LC_OEM_OPEN >> 24) & 0xff;

        address_space_write(&address_space_memory, info_addr,
                            MEMTXATTRS_UNSPECIFIED, info_buf, sizeof(info_buf));

        s->rx_words[0] = ((uint32_t)ELE_MSG_TAG_RESP << 24) |
                         ((uint32_t)cmd << 16) |
                         (2U << 8) |
                         ELE_MSG_VER;
        s->rx_words[1] = ELE_SUCCESS_IND; /* ELE_SUCCESS_IND */
        s->rx_count = 2;

        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.ele-mu: ELE_GET_INFO: wrote soc=0x%08x lc=0x%08x "
                      "to addr=0x%016" PRIx64 "\n",
                      ELE_GET_INFO_SOC_IMX95_REV10,
                      ELE_GET_INFO_LC_OEM_OPEN,
                      info_addr);
    }
    break;

     case ELE_TRNG_STATE_REQ:
        s->rx_words[0] =
            ((uint32_t)ELE_MSG_TAG_RESP << 24) |
            ((uint32_t)cmd << 16) |
            (ELE_TRNG_STATE_RESP_WORDS << 8) |
            ELE_MSG_VER;
        s->rx_words[1] = ELE_SUCCESS_IND;
        s->rx_words[2] = ELE_TRNG_CSAL_STATE_READY;
        s->rx_count = ELE_TRNG_STATE_RESP_WORDS;

        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.ele-mu: queued ELE_TRNG_STATE "
                      "state=0x%08x\n",
                      ELE_TRNG_CSAL_STATE_READY);
    break;

        case ELE_GET_FW_VERSION_REQ:
        s->rx_words[0] =
            ((uint32_t)ELE_MSG_TAG_RESP << 24) |
            ((uint32_t)cmd << 16) |
            (ELE_GET_FW_VERSION_RESP_WORDS << 8) |
            ver;
        s->rx_words[1] = ELE_SUCCESS_IND;
        s->rx_words[2] = ELE_FW_VERSION_STUB;
        s->rx_words[3] = 0U;
        s->rx_count = ELE_GET_FW_VERSION_RESP_WORDS;

        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.ele-mu: queued ELE_GET_FW_VERSION "
                      "version=0x%08x\n",
                      ELE_FW_VERSION_STUB);
        break;

    case ELE_GET_V2X_FW_STATE_REQ:
        s->rx_words[0] =
            ((uint32_t)ELE_MSG_TAG_RESP << 24) |
            ((uint32_t)cmd << 16) |
            (ELE_GET_V2X_FW_STATE_RESP_WORDS << 8) |
            ver;
        s->rx_words[1] = ELE_SUCCESS_IND;
        s->rx_words[2] = V2X_FW_AUTH_NORM_COMPLETE;
        s->rx_words[3] = 0U;
        s->rx_count = ELE_GET_V2X_FW_STATE_RESP_WORDS;

        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.ele-mu: queued ELE_GET_V2X_FW_STATE "
                      "state=0x%08x\n",
                      V2X_FW_AUTH_NORM_COMPLETE);
        break;


    case ELE_GET_RANDOM_REQ:
    {
        uint64_t random_addr;
        uint32_t requested_bytes;
        uint32_t returned_bytes;
        uint8_t random_data[ELE_GET_RANDOM_MAX_BYTES];
        MemTxResult result;

        random_addr = ((uint64_t)s->tx_words[1] << 32) |
                      (uint64_t)s->tx_words[2];
        requested_bytes = s->tx_words[3];
        returned_bytes = MIN(requested_bytes,
                             (uint32_t)ELE_GET_RANDOM_MAX_BYTES);

        memset(random_data, 0, sizeof(random_data));
        qemu_guest_getrandom_nofail(random_data, returned_bytes);

        result = address_space_write(&address_space_memory,
                                     random_addr,
                                     MEMTXATTRS_UNSPECIFIED,
                                     random_data,
                                     returned_bytes);

        s->rx_words[0] =
            ((uint32_t)ELE_MSG_TAG_RESP << 24) |
            ((uint32_t)cmd << 16) |
            (ELE_GET_RANDOM_RESP_WORDS << 8) |
            ver;
        s->rx_words[1] =
            result == MEMTX_OK ? ELE_SUCCESS_IND : 0U;
        s->rx_count = ELE_GET_RANDOM_RESP_WORDS;

        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.ele-mu: ELE_GET_RANDOM "
                      "addr=0x%016" PRIx64
                      " requested=%u returned=%u result=%u\n",
                      random_addr,
                      requested_bytes,
                      returned_bytes,
                      result);
        break;
    }


    default:
        s->rx_words[0] = ((uint32_t) ELE_MSG_TAG_RESP << 24) |
                         ((uint32_t) cmd << 16) |
                         (2U << 8) |
                         ELE_MSG_VER;
        s->rx_words[1] = ELE_SUCCESS_IND;
        s->rx_count = 2;

        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.ele-mu: unhandled ELE cmd 0x%02x, "
                      "returning generic success\n",
                      cmd);
        break;
    }
    imx95_ele_mu_update_mu3_irq(s);
}

static uint64_t imx95_ele_mu_read(void *opaque, hwaddr offset, unsigned size)
{
    Imx95EleMuState *s = opaque;
    hwaddr block_off = offset % IMX95_MU_BLOCK_SIZE;
    unsigned int block = offset / IMX95_MU_BLOCK_SIZE;
    uint32_t value = 0;

    if (offset >= IMX95_ELE_MU_SIZE) {
        return 0;
    }

    value = s->regs[offset / 4];

 if (block_off == IMX95_MU_PAR_OFFSET) {
 return IMX95_MU_PAR_4TR_4RR;
   }

    if (block_off == IMX95_MU_TSR_OFFSET) {
	return imx95_ele_mu_status(s, block, IMX95_MU_TSR_OFFSET);
    }

    if (block_off == IMX95_MU_RSR_OFFSET) {
       return imx95_ele_mu_status(s, block, IMX95_MU_RSR_OFFSET);
    }

    if (block_off >= IMX95_MU_RR_OFFSET &&
        block_off < IMX95_MU_RR_OFFSET + 0x10) {
        uint32_t idx = (block_off - IMX95_MU_RR_OFFSET) / 4;

	if (block == s->active_block && s->rx_pos < s->rx_count) {
            value = s->rx_words[s->rx_pos++];

            qemu_log_mask(LOG_GUEST_ERROR,
			     "imx95.ele-mu: RR%u read "
                          "block=0x%05" HWADDR_PRIx " "
                          "value=0x%08x rx_pos=%u rx_count=%u\n",
                          idx,
                          offset - block_off,
                          value,
                          s->rx_pos,
                          s->rx_count);

	    imx95_ele_mu_update_mu3_irq(s);
            return value;
        }

	qemu_log_mask(LOG_GUEST_ERROR,
              "imx95.ele-mu: RR%u read empty "
              "block=0x%05" HWADDR_PRIx " "
              "rx_pos=%u rx_count=%u\n",
              idx,
              offset - block_off,
              s->rx_pos,
              s->rx_count);

        return 0;
    }
    imx95_ele_mu_update_mu3_irq(s);
    return value;
}


static void imx95_ele_mu_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned size)
{
    Imx95EleMuState *s = opaque;
    hwaddr block_off = offset % IMX95_MU_BLOCK_SIZE;
    unsigned int block = offset / IMX95_MU_BLOCK_SIZE;

    if (offset >= IMX95_ELE_MU_SIZE) {
        return;
    }

    s->regs[offset / 4] = value;

    if (block_off == IMX95_MU_TCR_OFFSET ||
        block_off == IMX95_MU_RCR_OFFSET) {
        if (block == IMX95_ELE_MU3_BLOCK) {
            imx95_ele_mu_update_mu3_irq(s);
        }
        return;
    }

    /*
     * Transmit registers TR0..TR3.
     */
    if (block_off >= IMX95_MU_TR_OFFSET &&
        block_off < IMX95_MU_TR_OFFSET + 0x10) {
       uint32_t idx = (block_off - IMX95_MU_TR_OFFSET) / 4;
        uint32_t word = (uint32_t) value;

	uint32_t expected_idx;
	qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.ele-mu: TR%u write "
                      "block=0x%05" HWADDR_PRIx " "
                      "value=0x%08x\n",
                      idx,
                      offset - block_off,
                      word);

	if (s->tx_count == 0) {
	    if (idx != 0) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "imx95.ele-mu: ignoring TR%u write "
                              "without an active request\n",
                              idx);
                return;
            }
            s->tx_expected = (word >> 8) & 0xff;
	    if (s->tx_expected == 0 ||
                s->tx_expected > ARRAY_SIZE(s->tx_words)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "imx95.ele-mu: invalid request size %u "
                              "in header 0x%08x\n",
                              s->tx_expected,
                              word);
                s->tx_expected = 0;
                return;
            }

	    s->active_block = block;

            s->rx_count = 0;
            s->rx_pos = 0;
	     imx95_ele_mu_update_mu3_irq(s);
        } else if (block != s->active_block) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "imx95.ele-mu: request changed from block %u "
                          "to block %u\n",
                          s->active_block, block);
            return;
        }

        expected_idx = s->tx_count % 4;

        if (idx != expected_idx) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "imx95.ele-mu: unexpected TR%u write, "
                          "expected TR%u for request word %u of %u\n",
                          idx,
                          expected_idx,
                          s->tx_count,
                          s->tx_expected);
            return;
        }

	s->tx_words[s->tx_count] = word;
        s->tx_count++;

        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.ele-mu: stored request word %u of %u "
                      "from TR%u value=0x%08x\n",
                      s->tx_count,
                      s->tx_expected,
                      idx,
                      word);

        if (s->tx_count == s->tx_expected) {
            imx95_ele_mu_queue_response(s);
            s->tx_count = 0;
            s->tx_expected = 0;
        }
	return;
    }
}

static const MemoryRegionOps imx95_ele_mu_ops = {
    .read = imx95_ele_mu_read,
    .write = imx95_ele_mu_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};


static void imx95_ele_mu_set_ca55_mask(void *opaque, int n, int level)
{
    Imx95EleMuState *s = opaque;

    s->ca55_masked = level != 0;
    imx95_ele_mu_update_mu3_irq(s);
}

static void imx95_ele_mu_reset(DeviceState *dev)
{
    Imx95EleMuState *s = IMX95_ELE_MU(dev);

    memset(s->regs, 0, sizeof(s->regs));
    memset(s->tx_words, 0, sizeof(s->tx_words));
    s->tx_count = 0;
    s->tx_expected = 0;
    memset(s->rx_words, 0, sizeof(s->rx_words));
    s->rx_count = 0;
    s->rx_pos = 0;
    s->active_block = UINT_MAX;
    s->ca55_masked = false;
    s->mu3_irq_level = false;
    qemu_set_irq(s->mu3_irq, 0);
}

static void imx95_ele_mu_init(Object *obj)
{
    Imx95EleMuState *s = IMX95_ELE_MU(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_ele_mu_ops, s,
                          TYPE_IMX95_ELE_MU, IMX95_ELE_MU_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->mu3_irq);
    qdev_init_gpio_in_named(DEVICE(obj), imx95_ele_mu_set_ca55_mask,
                            "ca55-irq-mask", 1);
}

static const VMStateDescription vmstate_imx95_ele_mu = {
    .name = TYPE_IMX95_ELE_MU,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, Imx95EleMuState, IMX95_ELE_MU_SIZE / 4),
        VMSTATE_UINT32_ARRAY(tx_words, Imx95EleMuState, 8),
        VMSTATE_UINT32(tx_count, Imx95EleMuState),
        VMSTATE_UINT32(tx_expected, Imx95EleMuState),
        VMSTATE_UINT32_ARRAY(rx_words, Imx95EleMuState, 8),
        VMSTATE_UINT32(rx_count, Imx95EleMuState),
        VMSTATE_UINT32(rx_pos, Imx95EleMuState),
        VMSTATE_UINT32(active_block, Imx95EleMuState),
        VMSTATE_BOOL(ca55_masked, Imx95EleMuState),
        VMSTATE_BOOL(mu3_irq_level, Imx95EleMuState),
        VMSTATE_END_OF_LIST()
    }
};

static void imx95_ele_mu_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, imx95_ele_mu_reset);
    dc->vmsd = &vmstate_imx95_ele_mu;
    dc->desc = "i.MX95 ele mu";
}

static const TypeInfo imx95_ele_mu_types[] = {
    {
        .name = TYPE_IMX95_ELE_MU,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Imx95EleMuState),
        .instance_init = imx95_ele_mu_init,
        .class_init = imx95_ele_mu_class_init,
    },
};

DEFINE_TYPES(imx95_ele_mu_types)
