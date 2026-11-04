/*
 * NXP i.MX95 v2x dbg mu
 *
 * V2X debug message unit. Answers the START_RNG request with a success
 * response and the RNG ready state.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "system/memory.h"
#include "hw/misc/imx95_v2x_dbg_mu.h"

#include "hw/misc/imx95_mu_regs.h"




static uint32_t imx95_v2x_dbg_mu_rsr(Imx95V2xDbgMuState *s)
{
    if (s->rx_pos < s->rx_count) {
        return IMX95_MU_RX_FULL_BITS;
    }

    return 0;
}

static void imx95_v2x_dbg_mu_update_irq(Imx95V2xDbgMuState *s)
{
    uint32_t rcr;
    uint32_t rsr;
    bool level;

    rcr = s->regs[IMX95_MU_RCR_OFFSET / sizeof(uint32_t)];
    rsr = imx95_v2x_dbg_mu_rsr(s);
    level = (rcr & rsr & IMX95_MU_RX_FULL_BITS) != 0U;

    if (level != s->irq_level) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.v2x-dbg-mu: IRQ level %u -> %u "
                      "rcr=0x%08x rsr=0x%08x "
                      "rx-pos=%u rx-count=%u\n",
                      s->irq_level,
                      level,
                      rcr,
                      rsr,
                      s->rx_pos,
                      s->rx_count);
        s->irq_level = level;
    }

    qemu_set_irq(s->irq, level);
}

static void imx95_v2x_dbg_mu_queue_response(Imx95V2xDbgMuState *s)
{
    uint32_t header = s->tx_words[0];
    uint8_t version = header & 0xff;
    uint8_t words = (header >> 8) & 0xff;
    uint8_t command = (header >> 16) & 0xff;
    uint8_t tag = (header >> 24) & 0xff;

    s->rx_count = 0;
    s->rx_pos = 0;

    qemu_log_mask(LOG_GUEST_ERROR,
                  "imx95.v2x-dbg-mu: request "
                  "header=0x%08x version=0x%02x words=%u "
                  "command=0x%02x tag=0x%02x\n",
                  header,
                  version,
                  words,
                  command,
                  tag);

    if (version != V2X_DEBUG_MU_MSG_VERSION ||
        words != V2X_START_RNG_REQ_WORDS ||
        tag != V2X_DEBUG_MU_MSG_COMMAND_TAG) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.v2x-dbg-mu: invalid request header "
                      "0x%08x\n",
                      header);
        return;
    }

    switch (command) {
    case V2X_START_RNG_REQ:
        s->rx_words[0] =
            ((uint32_t)V2X_DEBUG_MU_MSG_RESPONSE_TAG << 24) |
            ((uint32_t)command << 16) |
            (V2X_START_RNG_RESPONSE_WORDS << 8) |
            V2X_DEBUG_MU_MSG_VERSION;
        s->rx_words[1] = V2X_SUCCESS_IND;
        s->rx_words[2] = V2X_START_RNG_STATE_READY;
        s->rx_count = V2X_START_RNG_RESPONSE_WORDS;

        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.v2x-dbg-mu: queued START_RNG response "
                      "header=0x%08x status=0x%08x state=0x%08x\n",
                      s->rx_words[0],
                      s->rx_words[1],
                      s->rx_words[2]);
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "imx95.v2x-dbg-mu: unsupported command 0x%02x\n",
                      command);
        break;
    }

    imx95_v2x_dbg_mu_update_irq(s);
}

static uint64_t imx95_v2x_dbg_mu_read(void *opaque,
                                      hwaddr offset,
                                      unsigned size)
{
    Imx95V2xDbgMuState *s = opaque;
    uint32_t value;
    uint32_t index;

    if (size != sizeof(uint32_t) ||
        (offset & 3U) != 0U ||
        offset >= IMX95_V2X_DBG_MU_SIZE) {
        return 0;
    }

    switch (offset) {
    case IMX95_MU_PAR_OFFSET:
        return IMX95_MU_PAR_4TR_4RR;

    case IMX95_MU_TSR_OFFSET:
        return IMX95_MU_TX_READY_BITS;

    case IMX95_MU_RSR_OFFSET:
        return imx95_v2x_dbg_mu_rsr(s);

    default:
        break;
    }

    if (offset >= IMX95_MU_RR_OFFSET &&
        offset < IMX95_MU_RR_OFFSET + 0x10) {
        index = (offset - IMX95_MU_RR_OFFSET) / sizeof(uint32_t);

        if (s->rx_pos >= s->rx_count) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "imx95.v2x-dbg-mu: RR%u read while empty\n",
                          index);
            return 0;
        }

        value = s->rx_words[s->rx_pos++];

        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.v2x-dbg-mu: RR%u read "
                      "value=0x%08x rx-pos=%u rx-count=%u\n",
                      index,
                      value,
                      s->rx_pos,
                      s->rx_count);

        if (s->rx_pos == s->rx_count) {
            s->rx_pos = 0;
            s->rx_count = 0;
        }

        imx95_v2x_dbg_mu_update_irq(s);
        return value;
    }

    return s->regs[offset / sizeof(uint32_t)];
}

static void imx95_v2x_dbg_mu_write(void *opaque,
                                   hwaddr offset,
                                   uint64_t value,
                                   unsigned size)
{
    Imx95V2xDbgMuState *s = opaque;
    uint32_t word = value;
    uint32_t index;
    uint32_t expected_index;

    if (size != sizeof(uint32_t) ||
        (offset & 3U) != 0U ||
        offset >= IMX95_V2X_DBG_MU_SIZE) {
        return;
    }

    if (offset == IMX95_MU_TCR_OFFSET ||
        offset == IMX95_MU_RCR_OFFSET) {
        s->regs[offset / sizeof(uint32_t)] = word;
        imx95_v2x_dbg_mu_update_irq(s);
        return;
    }

    if (offset < IMX95_MU_TR_OFFSET ||
        offset >= IMX95_MU_TR_OFFSET + 0x10) {
        s->regs[offset / sizeof(uint32_t)] = word;
        return;
    }

    index = (offset - IMX95_MU_TR_OFFSET) / sizeof(uint32_t);

    if (s->tx_count == 0) {
        if (index != 0) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "imx95.v2x-dbg-mu: ignoring TR%u write "
                          "without a request header\n",
                          index);
            return;
        }

        if (s->rx_pos < s->rx_count) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "imx95.v2x-dbg-mu: ignoring new request "
                          "while response is pending\n");
            return;
        }

        s->tx_expected = (word >> 8) & 0xff;

        if (s->tx_expected == 0 ||
            s->tx_expected > ARRAY_SIZE(s->tx_words)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "imx95.v2x-dbg-mu: invalid request size %u "
                          "in header 0x%08x\n",
                          s->tx_expected,
                          word);
            s->tx_expected = 0;
            return;
        }
    }

    expected_index = s->tx_count % 4;
    if (index != expected_index) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.v2x-dbg-mu: unexpected TR%u write, "
                      "expected TR%u\n",
                      index,
                      expected_index);
        return;
    }

    s->tx_words[s->tx_count++] = word;

    qemu_log_mask(LOG_GUEST_ERROR,
                  "imx95.v2x-dbg-mu: TR%u write "
                  "value=0x%08x request-word=%u/%u\n",
                  index,
                  word,
                  s->tx_count,
                  s->tx_expected);

    if (s->tx_count == s->tx_expected) {
        imx95_v2x_dbg_mu_queue_response(s);
        s->tx_count = 0;
        s->tx_expected = 0;
    }
}

static const MemoryRegionOps imx95_v2x_dbg_mu_ops = {
    .read = imx95_v2x_dbg_mu_read,
    .write = imx95_v2x_dbg_mu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};


static void imx95_v2x_dbg_mu_reset(DeviceState *dev)
{
    Imx95V2xDbgMuState *s = IMX95_V2X_DBG_MU(dev);

    memset(s->regs, 0, sizeof(s->regs));
    memset(s->tx_words, 0, sizeof(s->tx_words));
    s->tx_count = 0;
    s->tx_expected = 0;
    memset(s->rx_words, 0, sizeof(s->rx_words));
    s->rx_count = 0;
    s->rx_pos = 0;
    s->irq_level = false;
    qemu_set_irq(s->irq, 0);
}

static void imx95_v2x_dbg_mu_init(Object *obj)
{
    Imx95V2xDbgMuState *s = IMX95_V2X_DBG_MU(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_v2x_dbg_mu_ops, s,
                          TYPE_IMX95_V2X_DBG_MU, IMX95_V2X_DBG_MU_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static const VMStateDescription vmstate_imx95_v2x_dbg_mu = {
    .name = TYPE_IMX95_V2X_DBG_MU,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, Imx95V2xDbgMuState, IMX95_V2X_DBG_MU_SIZE / sizeof(uint32_t)),
        VMSTATE_UINT32_ARRAY(tx_words, Imx95V2xDbgMuState, 8),
        VMSTATE_UINT32(tx_count, Imx95V2xDbgMuState),
        VMSTATE_UINT32(tx_expected, Imx95V2xDbgMuState),
        VMSTATE_UINT32_ARRAY(rx_words, Imx95V2xDbgMuState, 8),
        VMSTATE_UINT32(rx_count, Imx95V2xDbgMuState),
        VMSTATE_UINT32(rx_pos, Imx95V2xDbgMuState),
        VMSTATE_BOOL(irq_level, Imx95V2xDbgMuState),
        VMSTATE_END_OF_LIST()
    }
};

static void imx95_v2x_dbg_mu_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, imx95_v2x_dbg_mu_reset);
    dc->vmsd = &vmstate_imx95_v2x_dbg_mu;
    dc->desc = "i.MX95 v2x dbg mu";
}

static const TypeInfo imx95_v2x_dbg_mu_types[] = {
    {
        .name = TYPE_IMX95_V2X_DBG_MU,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Imx95V2xDbgMuState),
        .instance_init = imx95_v2x_dbg_mu_init,
        .class_init = imx95_v2x_dbg_mu_class_init,
    },
};

DEFINE_TYPES(imx95_v2x_dbg_mu_types)
