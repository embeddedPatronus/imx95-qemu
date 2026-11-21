/*
 * NXP i.MX95 SC MU
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/imx95_sc_mu.h"
#include "migration/vmstate.h"

#define SC_RPC_RESULT_NONE 0U

static unsigned int imx95_sc_mu_reg_index(hwaddr offset, hwaddr base)
{
    return (offset - base) / sizeof(uint32_t);
}

static bool imx95_sc_mu_in_reg_range(hwaddr offset, hwaddr base)
{
    return offset >= base &&
           offset < base + IMX95_SC_MU_NUM_REGS * sizeof(uint32_t);
}

static void imx95_sc_mu_clear_transaction(Imx95ScMuState *s)
{
    s->tx_count = 0;
    s->tx_size = 0;
    s->rx_ready = false;
    s->rx_count = 0;
    s->rx_size = 0;
}

static void imx95_sc_mu_build_response(Imx95ScMuState *s)
{
    memset(s->rr, 0, sizeof(s->rr));
    s->rr[0] = SC_RPC_VERSION |
               (1U << 8) |
               (SC_RPC_SVC_RETURN << 16) |
               (SC_RPC_RESULT_NONE << 24);
    s->rx_size = 1;
    s->rx_count = 0;
    s->rx_ready = true;
}

static uint64_t imx95_sc_mu_read(void *opaque, hwaddr offset,
                                 unsigned int size)
{
    Imx95ScMuState *s = opaque;

    if (offset == IMX8_MU_SR) {
        uint32_t status = IMX8_MU_SR_TE_ALL;

        if (s->rx_ready) {
            status |= IMX8_MU_SR_RF_ALL;
        }
        return status;
    }

    if (offset == IMX8_MU_CR) {
        return 0;
    }

    if (imx95_sc_mu_in_reg_range(offset, IMX8_MU_RR0)) {
        unsigned int index = imx95_sc_mu_reg_index(offset, IMX8_MU_RR0);
        uint32_t value = s->rr[index];

        if (s->rx_ready) {
            s->rx_count++;
            if (s->rx_count >= s->rx_size) {
                imx95_sc_mu_clear_transaction(s);
            }
        }
        return value;
    }

    if (imx95_sc_mu_in_reg_range(offset, IMX8_MU_TR0)) {
        unsigned int index = imx95_sc_mu_reg_index(offset, IMX8_MU_TR0);

        return s->tr[index];
    }

    return 0;
}

static void imx95_sc_mu_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned int size)
{
    Imx95ScMuState *s = opaque;

    if (offset == IMX8_MU_CR) {
        return;
    }

    if (imx95_sc_mu_in_reg_range(offset, IMX8_MU_TR0)) {
        unsigned int index = imx95_sc_mu_reg_index(offset, IMX8_MU_TR0);

        s->tr[index] = value;
        if (index == 0) {
            uint32_t message_size = (value >> 8) & UINT8_MAX;

            if (message_size == 0 || message_size > SC_RPC_MAX_MSG) {
                message_size = 1;
            }
            s->tx_size = message_size;
            s->tx_count = 1;
        } else if (s->tx_size > 0 &&
                   s->tx_count < SC_RPC_MAX_MSG) {
            s->tx_count++;
        }

        /* Answer once the header's word count has arrived */
        if (s->tx_size > 0 && s->tx_count >= s->tx_size) {
            imx95_sc_mu_build_response(s);
        }
    }
}

static const MemoryRegionOps imx95_sc_mu_ops = {
    .read = imx95_sc_mu_read,
    .write = imx95_sc_mu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void imx95_sc_mu_reset(DeviceState *dev)
{
    Imx95ScMuState *s = IMX95_SC_MU(dev);

    memset(s->tr, 0, sizeof(s->tr));
    memset(s->rr, 0, sizeof(s->rr));
    imx95_sc_mu_clear_transaction(s);
}

static int imx95_sc_mu_post_load(void *opaque, int version_id)
{
    Imx95ScMuState *s = opaque;

    if (s->tx_count > SC_RPC_MAX_MSG ||
        s->tx_size > SC_RPC_MAX_MSG ||
        s->rx_count > IMX95_SC_MU_NUM_REGS ||
        s->rx_size > IMX95_SC_MU_NUM_REGS ||
        s->rx_count > s->rx_size ||
        (s->rx_ready && s->rx_size == 0)) {
        return -EINVAL;
    }

    return 0;
}

static void imx95_sc_mu_init(Object *obj)
{
    Imx95ScMuState *s = IMX95_SC_MU(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_sc_mu_ops, s,
                          TYPE_IMX95_SC_MU, IMX95_SC_MU_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription vmstate_imx95_sc_mu = {
    .name = TYPE_IMX95_SC_MU,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = imx95_sc_mu_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(tr, Imx95ScMuState,
                             IMX95_SC_MU_NUM_REGS),
        VMSTATE_UINT32_ARRAY(rr, Imx95ScMuState,
                             IMX95_SC_MU_NUM_REGS),
        VMSTATE_UINT32(tx_count, Imx95ScMuState),
        VMSTATE_UINT32(tx_size, Imx95ScMuState),
        VMSTATE_BOOL(rx_ready, Imx95ScMuState),
        VMSTATE_UINT32(rx_count, Imx95ScMuState),
        VMSTATE_UINT32(rx_size, Imx95ScMuState),
        VMSTATE_END_OF_LIST()
    },
};

static void imx95_sc_mu_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, imx95_sc_mu_reset);
    dc->vmsd = &vmstate_imx95_sc_mu;
    dc->desc = "i.MX95 SC MU";
}

static const TypeInfo imx95_sc_mu_types[] = {
    {
        .name = TYPE_IMX95_SC_MU,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Imx95ScMuState),
        .instance_init = imx95_sc_mu_init,
        .class_init = imx95_sc_mu_class_init,
    },
};

DEFINE_TYPES(imx95_sc_mu_types)

