/*
 * NXP i.MX95 sc mu
 *
 * Message unit of the SC RPC interface. Answers every SC RPC request with
 * a success response.
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
#include "hw/misc/imx95_sc_mu.h"

static void imx95_sc_mu_build_response(Imx95ScMuState *s)
{
    s->rr[0] = (SC_RPC_VERSION)        |
               (1u          << 8)      |   /* size = 1 word */
               (SC_RPC_SVC_RETURN << 16)|
               (0u           << 24);       /* func/result = 0 = SC_ERR_NONE */
    s->rx_size  = 1;
    s->rx_count = 0;
    s->rx_ready = true;
}

static uint64_t imx95_sc_mu_read(void *opaque, hwaddr offset, unsigned size)
{
    Imx95ScMuState *s = opaque;

    if (offset == IMX8_MU_SR) {
        uint32_t sr = IMX8_MU_SR_TE_ALL;   /* all TX regs always empty */
        if (s->rx_ready) {
            sr |= IMX8_MU_SR_RF_ALL;     /* all RX regs full when response ready */
        }
        return sr;
    }

    if (offset == IMX8_MU_CR) {
        return 0;
    }

    /* RR[0..3] */
    if (offset >= IMX8_MU_RR0 && offset < IMX8_MU_RR0 + 16u) {
        unsigned idx = (offset - IMX8_MU_RR0) / 4;
        uint32_t val = (idx < 4) ? s->rr[idx] : 0;
        if (s->rx_ready) {
            s->rx_count++;
            if (s->rx_count >= s->rx_size) {
                s->rx_ready = false;
                s->rx_count = 0;
                s->tx_count = 0;
                s->tx_size  = 0;
            }
        }
        return val;
    }

    /* TR shadow (read-back) */
    if (offset < IMX8_MU_TR0 + 16u) {
        unsigned idx = (offset - IMX8_MU_TR0) / 4;
        return (idx < 4) ? s->tr[idx] : 0;
    }

    return 0;
}

static void imx95_sc_mu_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned size)
{
    Imx95ScMuState *s = opaque;

    if (offset == IMX8_MU_CR) {
        return;
    }

    /* TR[0..3] */
    if (offset < IMX8_MU_TR0 + 16u) {
        unsigned idx = (offset - IMX8_MU_TR0) / 4;
        if (idx < 4) {
            s->tr[idx] = (uint32_t)value;
        }

        if (idx == 0) {
            uint8_t msg_size = (value >> 8) & 0xff;
            if (msg_size == 0 || msg_size > SC_RPC_MAX_MSG) {
                msg_size = 1;
            }
            s->tx_size  = msg_size;
            s->tx_count = 1;
        } else {
            if (s->tx_count < SC_RPC_MAX_MSG) {
                s->tx_count++;
            }
        }

        if (s->tx_size > 0 && s->tx_count >= s->tx_size) {
            imx95_sc_mu_build_response(s);
        }
        return;
    }
}

static const MemoryRegionOps imx95_sc_mu_ops = {
    .read  = imx95_sc_mu_read,
    .write = imx95_sc_mu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
};


static void imx95_sc_mu_reset(DeviceState *dev)
{
    Imx95ScMuState *s = IMX95_SC_MU(dev);

    memset(s->tr, 0, sizeof(s->tr));
    memset(s->rr, 0, sizeof(s->rr));
    s->tx_count = 0;
    s->tx_size = 0;
    s->rx_ready = false;
    s->rx_count = 0;
    s->rx_size = 0;
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
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(tr, Imx95ScMuState, 4),
        VMSTATE_UINT32_ARRAY(rr, Imx95ScMuState, 4),
        VMSTATE_UINT32(tx_count, Imx95ScMuState),
        VMSTATE_UINT32(tx_size, Imx95ScMuState),
        VMSTATE_BOOL(rx_ready, Imx95ScMuState),
        VMSTATE_UINT32(rx_count, Imx95ScMuState),
        VMSTATE_UINT32(rx_size, Imx95ScMuState),
        VMSTATE_END_OF_LIST()
    }
};

static void imx95_sc_mu_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, imx95_sc_mu_reset);
    dc->vmsd = &vmstate_imx95_sc_mu;
    dc->desc = "i.MX95 sc mu";
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
