/*
 * NXP i.MX LPI2C controller
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/i2c/imx_lpi2c.h"
#include "hw/core/irq.h"
#include "migration/vmstate.h"
#include "qemu/module.h"

/* Register offsets */
#define LPI2C_VERID       0x000
#define LPI2C_PARAM       0x004
#define LPI2C_MCR         0x010
#define LPI2C_MSR         0x014
#define LPI2C_MIER        0x018
#define LPI2C_MDER        0x01C
#define LPI2C_MCFGR0      0x020
#define LPI2C_MCFGR1      0x024
#define LPI2C_MCFGR2      0x028
#define LPI2C_MCFGR3      0x02C
#define LPI2C_MDMR        0x040
#define LPI2C_MCCR0       0x048
#define LPI2C_MCCR1       0x050
#define LPI2C_MFCR        0x058
#define LPI2C_MFSR        0x05C
#define LPI2C_MTDR        0x060
#define LPI2C_MRDR        0x070

/* MDER bits */
#define LPI2C_MDER_TDDE   BIT(0)
#define LPI2C_MDER_RDDE   BIT(1)

/* MFCR fields */
#define LPI2C_MFCR_RXWATER_SHIFT 16
#define LPI2C_MFCR_RXWATER_MASK  0x7

#define LPI2C_FIFO_WORDS  8
#define LPI2C_VERID_VALUE 0x00010000
#define LPI2C_PARAM_VALUE 0x00000303

/* MCR bits */
#define LPI2C_MCR_MEN     BIT(0)
#define LPI2C_MCR_RST     BIT(1)
#define LPI2C_MCR_DOZEN   BIT(2)
#define LPI2C_MCR_DBGEN   BIT(3)
#define LPI2C_MCR_RTF     BIT(8)
#define LPI2C_MCR_RRF     BIT(9)

/* MSR bits */
#define LPI2C_MSR_TDF     BIT(0)
#define LPI2C_MSR_RDF     BIT(1)
#define LPI2C_MSR_EPF     BIT(8)
#define LPI2C_MSR_SDF     BIT(9)
#define LPI2C_MSR_NDF     BIT(10)
#define LPI2C_MSR_ALF     BIT(11)
#define LPI2C_MSR_FEF     BIT(12)
#define LPI2C_MSR_PLTF    BIT(13)
#define LPI2C_MSR_DMF     BIT(14)
#define LPI2C_MSR_MBF     BIT(24)
#define LPI2C_MSR_BBF     BIT(25)

#define LPI2C_MSR_W1C_MASK (LPI2C_MSR_EPF | LPI2C_MSR_SDF | \
                            LPI2C_MSR_NDF | LPI2C_MSR_ALF | \
                            LPI2C_MSR_FEF | LPI2C_MSR_PLTF | \
                            LPI2C_MSR_DMF)

#define LPI2C_MSR_BUSY_MASK (LPI2C_MSR_MBF | LPI2C_MSR_BBF)

/* MRDR bits */
#define LPI2C_MRDR_RXEMPTY BIT(14)

/* MTDR command encoding */
#define LPI2C_MTDR_CMD_SHIFT 8
#define LPI2C_MTDR_CMD_MASK  0x700
#define LPI2C_MTDR_DATA_MASK 0xff

#define LPI2C_MTDR_CMD_TX_DATA 0x0
#define LPI2C_MTDR_CMD_RX_DATA 0x1
#define LPI2C_MTDR_CMD_STOP    0x2
#define LPI2C_MTDR_CMD_START   0x4

static void imx_lpi2c_master_reset(IMXLPI2CState *s);

static unsigned int imx_lpi2c_rx_count(const IMXLPI2CState *s)
{
    return s->rx_len - s->rx_pos;
}

static unsigned int imx_lpi2c_rx_water(const IMXLPI2CState *s)
{
    return (s->regs[LPI2C_MFCR / 4] >> LPI2C_MFCR_RXWATER_SHIFT) &
           LPI2C_MFCR_RXWATER_MASK;
}

static uint32_t imx_lpi2c_msr_value(const IMXLPI2CState *s)
{
    uint32_t msr = s->regs[LPI2C_MSR / 4];

    msr |= LPI2C_MSR_TDF;

    if (imx_lpi2c_rx_count(s) > imx_lpi2c_rx_water(s)) {
        msr |= LPI2C_MSR_RDF;
    } else {
        msr &= ~LPI2C_MSR_RDF;
    }

    return msr;
}

static bool imx_lpi2c_tx_dma_level(const IMXLPI2CState *s)
{
    return s->regs[LPI2C_MDER / 4] & LPI2C_MDER_TDDE;
}

static bool imx_lpi2c_rx_dma_level(const IMXLPI2CState *s)
{
    return (s->regs[LPI2C_MDER / 4] & LPI2C_MDER_RDDE) &&
           imx_lpi2c_rx_count(s) > imx_lpi2c_rx_water(s);
}

static void imx_lpi2c_update_outputs(IMXLPI2CState *s, bool force)
{
    bool tx = imx_lpi2c_tx_dma_level(s);
    bool rx = imx_lpi2c_rx_dma_level(s);

    qemu_set_irq(s->irq, (imx_lpi2c_msr_value(s) &
                          s->regs[LPI2C_MIER / 4]) != 0);

    if (force || tx != s->tx_req) {
        s->tx_req = tx;
        qemu_set_irq(s->tx_dma_req, tx);
    }
    if (force || rx != s->rx_req) {
        s->rx_req = rx;
        qemu_set_irq(s->rx_dma_req, rx);
    }
}

static void imx_lpi2c_update_irq(IMXLPI2CState *s)
{
    imx_lpi2c_update_outputs(s, false);
}

static void imx_lpi2c_do_stop(IMXLPI2CState *s)
{
    i2c_end_transfer(s->bus);
    s->stop_pending = false;
    s->regs[LPI2C_MSR / 4] |= LPI2C_MSR_TDF | LPI2C_MSR_SDF |
                                 LPI2C_MSR_EPF;
    s->regs[LPI2C_MSR / 4] &= ~LPI2C_MSR_BUSY_MASK;
}

static void imx_lpi2c_fill_rx(IMXLPI2CState *s)
{
    if (s->rx_pos == s->rx_len) {
        s->rx_pos = s->rx_len = 0;
    }
    while (s->rx_pending &&
           imx_lpi2c_rx_count(s) < LPI2C_FIFO_WORDS) {
        if (s->rx_len == sizeof(s->rx_fifo)) {
            memmove(s->rx_fifo, s->rx_fifo + s->rx_pos,
                    imx_lpi2c_rx_count(s));
            s->rx_len -= s->rx_pos;
            s->rx_pos = 0;
        }
        s->rx_fifo[s->rx_len++] = i2c_recv(s->bus);
        s->rx_pending--;
    }
    if (!s->rx_pending && s->stop_pending) {
        imx_lpi2c_do_stop(s);
    }
}

static void imx_lpi2c_master_reset(IMXLPI2CState *s)
{
    /* End any active transaction before resetting the master state. */
    i2c_end_transfer(s->bus);

    s->regs[LPI2C_MCR / 4] = 0;
    s->regs[LPI2C_MSR / 4] = LPI2C_MSR_TDF;
    s->regs[LPI2C_MIER / 4] = 0;
    s->regs[LPI2C_MDER / 4] = 0;
    s->regs[LPI2C_MCFGR0 / 4] = 0;
    s->regs[LPI2C_MCFGR1 / 4] = 0;
    s->regs[LPI2C_MCFGR2 / 4] = 0;
    s->regs[LPI2C_MCFGR3 / 4] = 0;
    s->regs[LPI2C_MDMR / 4] = 0;
    s->regs[LPI2C_MCCR0 / 4] = 0;
    s->regs[LPI2C_MCCR1 / 4] = 0;
    s->regs[LPI2C_MFCR / 4] = 0;
    s->regs[LPI2C_MFSR / 4] = 0;
    s->regs[LPI2C_MTDR / 4] = 0;
    s->regs[LPI2C_MRDR / 4] = 0;

    s->rx_len = 0;
    s->rx_pos = 0;
    s->rx_pending = 0;
    s->stop_pending = false;

    imx_lpi2c_update_irq(s);
}

static uint64_t imx_lpi2c_read(void *opaque, hwaddr offset,
                                 unsigned int size)
{
    IMXLPI2CState *s = opaque;

    if (offset >= IMX_LPI2C_MMIO_SIZE) {
        return 0;
    }

    switch (offset) {
    case LPI2C_MSR:
        return imx_lpi2c_msr_value(s);

    case LPI2C_MFSR:
        /*
         * TXCOUNT: bits 7:0 and RXCOUNT: bits 23:16.
         */
        return (imx_lpi2c_rx_count(s) & 0xff) << 16;

    case LPI2C_MRDR:
        if (imx_lpi2c_rx_count(s)) {
            uint8_t value = s->rx_fifo[s->rx_pos++];

            imx_lpi2c_fill_rx(s);
            imx_lpi2c_update_irq(s);
            return value;
        }

        return LPI2C_MRDR_RXEMPTY;

    default:
        return s->regs[offset / 4];
    }
}

static void imx_lpi2c_write(void *opaque, hwaddr offset,
                            uint64_t value, unsigned size)
{
    IMXLPI2CState *s = opaque;
    uint32_t v = value;

    if (offset >= IMX_LPI2C_MMIO_SIZE) {
        return;
    }

    switch (offset) {
    case LPI2C_MSR:
        s->regs[LPI2C_MSR / 4] &= ~(v & LPI2C_MSR_W1C_MASK);
        s->regs[LPI2C_MSR / 4] |= LPI2C_MSR_TDF;
        break;

    case LPI2C_MTDR:
    {
        uint32_t cmd = (v & LPI2C_MTDR_CMD_MASK) >>
                       LPI2C_MTDR_CMD_SHIFT;
        uint32_t data = v & LPI2C_MTDR_DATA_MASK;
        int ret;

        s->regs[LPI2C_MTDR / 4] = v;

        switch (cmd) {
        case LPI2C_MTDR_CMD_START:
            /*
             * START command data convention:
             *   bits [7:1] = 7-bit address
             *   bit 0      = read/write
             */
            s->regs[LPI2C_MSR / 4] &=
                ~(LPI2C_MSR_NDF | LPI2C_MSR_SDF |
                  LPI2C_MSR_EPF);

            ret = i2c_start_transfer(s->bus, data >> 1, data & 1);
            if (ret) {
                s->regs[LPI2C_MSR / 4] |= LPI2C_MSR_NDF |
                                             LPI2C_MSR_BUSY_MASK;
            } else {
                s->regs[LPI2C_MSR / 4] &= ~LPI2C_MSR_NDF;
                s->regs[LPI2C_MSR / 4] |= LPI2C_MSR_BUSY_MASK;
            }

            s->regs[LPI2C_MSR / 4] |= LPI2C_MSR_TDF;
            break;

        case LPI2C_MTDR_CMD_TX_DATA:
            ret = i2c_send(s->bus, data);
            if (ret) {
                i2c_end_transfer(s->bus);
                s->regs[LPI2C_MSR / 4] |= LPI2C_MSR_NDF;
            } else {
                s->regs[LPI2C_MSR / 4] &= ~LPI2C_MSR_NDF;
            }
            s->regs[LPI2C_MSR / 4] |= LPI2C_MSR_TDF;
            break;

        case LPI2C_MTDR_CMD_RX_DATA:
        {
            unsigned int count = data + 1;

            s->rx_pending += count;
            imx_lpi2c_fill_rx(s);
            s->regs[LPI2C_MSR / 4] |= LPI2C_MSR_TDF;
            break;
        }

        case LPI2C_MTDR_CMD_STOP:
            if (s->rx_pending) {
                s->stop_pending = true;
            } else {
                imx_lpi2c_do_stop(s);
            }
            break;

        default:
            s->regs[LPI2C_MSR / 4] |= LPI2C_MSR_TDF;
            break;
        }

        break;
    }

    case LPI2C_MCR:
    {
        uint32_t old_mcr = s->regs[LPI2C_MCR / 4];

        if (v & LPI2C_MCR_RST) {
            imx_lpi2c_master_reset(s);
            break;
        }

        if (v & LPI2C_MCR_RRF) {
            s->rx_len = 0;
            s->rx_pos = 0;
            s->regs[LPI2C_MSR / 4] &= ~LPI2C_MSR_RDF;
        }

        if (v & LPI2C_MCR_RTF) {
            s->rx_pending = 0;
            s->stop_pending = false;
            s->regs[LPI2C_MSR / 4] |= LPI2C_MSR_TDF;
        }

        if ((old_mcr & LPI2C_MCR_MEN) &&
            !(v & LPI2C_MCR_MEN)) {
            i2c_end_transfer(s->bus);
            s->regs[LPI2C_MSR / 4] &= ~LPI2C_MSR_BUSY_MASK;
        }

        s->regs[LPI2C_MCR / 4] =
            v & ~(LPI2C_MCR_RST |
                  LPI2C_MCR_RTF |
                  LPI2C_MCR_RRF);
        s->regs[LPI2C_MSR / 4] |= LPI2C_MSR_TDF;
        break;
    }

    default:
        s->regs[offset / 4] = v;
        break;
    }

    imx_lpi2c_update_irq(s);
}

static const MemoryRegionOps imx_lpi2c_ops = {
    .read = imx_lpi2c_read,
    .write = imx_lpi2c_write,
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

static void imx_lpi2c_reset(DeviceState *dev)
{
    IMXLPI2CState *s = IMX_LPI2C(dev);

    memset(s->regs, 0, sizeof(s->regs));

    s->regs[LPI2C_VERID / 4] = LPI2C_VERID_VALUE;
    s->regs[LPI2C_PARAM / 4] = LPI2C_PARAM_VALUE;

    imx_lpi2c_master_reset(s);
}

static int imx_lpi2c_post_load(void *opaque, int version_id)
{
    IMXLPI2CState *s = opaque;

    if (s->rx_len > IMX_LPI2C_RX_FIFO_SIZE ||
        s->rx_pos > s->rx_len) {
        return -EINVAL;
    }

    imx_lpi2c_update_outputs(s, true);

    return 0;
}

static const VMStateDescription vmstate_imx_lpi2c = {
    .name = TYPE_IMX_LPI2C,
    .version_id = 2,
    .minimum_version_id = 1,
    .post_load = imx_lpi2c_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, IMXLPI2CState,
                             IMX_LPI2C_MMIO_SIZE / 4),
        VMSTATE_UINT8_ARRAY(rx_fifo, IMXLPI2CState,
                            IMX_LPI2C_RX_FIFO_SIZE),
        VMSTATE_UINT32(rx_len, IMXLPI2CState),
        VMSTATE_UINT32(rx_pos, IMXLPI2CState),
        VMSTATE_UINT32_V(rx_pending, IMXLPI2CState, 2),
        VMSTATE_BOOL_V(stop_pending, IMXLPI2CState, 2),
        VMSTATE_BOOL_V(tx_req, IMXLPI2CState, 2),
        VMSTATE_BOOL_V(rx_req, IMXLPI2CState, 2),
        VMSTATE_END_OF_LIST()
    },
};

static void imx_lpi2c_init(Object *obj)
{
    IMXLPI2CState *s = IMX_LPI2C(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &imx_lpi2c_ops, s,
                          TYPE_IMX_LPI2C, IMX_LPI2C_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_out_named(DEVICE(obj), &s->tx_dma_req,
                             "tx-dma-req", 1);
    qdev_init_gpio_out_named(DEVICE(obj), &s->rx_dma_req,
                             "rx-dma-req", 1);

}

static void imx_lpi2c_realize(DeviceState *dev, Error **errp)
{
    IMXLPI2CState *s = IMX_LPI2C(dev);
    g_autofree char *name =
        g_strdup(object_get_canonical_path_component(OBJECT(dev)));

    s->bus = i2c_init_bus(dev, name);
}

static void imx_lpi2c_unrealize(DeviceState *dev)
{
    IMXLPI2CState *s = IMX_LPI2C(dev);

    i2c_end_transfer(s->bus);
}

static void imx_lpi2c_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = imx_lpi2c_realize;
    dc->unrealize = imx_lpi2c_unrealize;
    device_class_set_legacy_reset(dc, imx_lpi2c_reset);
    dc->vmsd = &vmstate_imx_lpi2c;
    dc->desc = "NXP i.MX LPI2C controller";
}

static const TypeInfo imx_lpi2c_info = {
    .name          = TYPE_IMX_LPI2C,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMXLPI2CState),
    .instance_init = imx_lpi2c_init,
    .class_init    = imx_lpi2c_class_init,
};

static void imx_lpi2c_register_types(void)
{
    type_register_static(&imx_lpi2c_info);
}

type_init(imx_lpi2c_register_types)

