/*
 * NXP i.MX LPI2C controller
 *
 * Initial reusable model:
 *   - SysBus MMIO controller
 *   - creates a QEMU I2C child bus
 *   - maps MTDR START/TX/RX/STOP commands to QEMU I2C core
 *   - provides polling-friendly MSR/MRDR behavior
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/i2c/imx_lpi2c.h"
#include "hw/core/irq.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
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

static void imx_lpi2c_update_irq(IMXLPI2CState *s)
{
    uint32_t msr = s->regs[LPI2C_MSR / 4];
    uint32_t mier = s->regs[LPI2C_MIER / 4];

    qemu_set_irq(s->irq, (msr & mier) != 0);
}

static void imx_lpi2c_push_rx(IMXLPI2CState *s, uint8_t value)
{
    if (s->rx_len < sizeof(s->rx_fifo)) {
        s->rx_fifo[s->rx_len++] = value;
        s->regs[LPI2C_MSR / 4] |= LPI2C_MSR_RDF;
    }
}

static uint32_t imx_lpi2c_msr_value(IMXLPI2CState *s)
{
    uint32_t msr = s->regs[LPI2C_MSR / 4];

    /* TX FIFO appears ready in this simple model. */
    msr |= LPI2C_MSR_TDF;

    if (s->rx_pos < s->rx_len) {
        msr |= LPI2C_MSR_RDF;
    } else {
        msr &= ~LPI2C_MSR_RDF;
    }

    return msr;
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

    qemu_set_irq(s->irq, 0);
}

static uint64_t imx_lpi2c_read(void *opaque, hwaddr offset, unsigned size)
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
         * TXCOUNT is in bits 7:0 and RXCOUNT is in bits 23:16.
         * This synchronous model has no pending TX FIFO entries.
         */
        return ((s->rx_len - s->rx_pos) & 0xff) << 16;

    case LPI2C_MRDR:
        if (s->rx_pos < s->rx_len) {
            uint8_t value = s->rx_fifo[s->rx_pos++];

            if (s->rx_pos >= s->rx_len) {
                s->rx_len = 0;
                s->rx_pos = 0;
                s->regs[LPI2C_MSR / 4] &= ~LPI2C_MSR_RDF;
            }

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
        /*
         * Event and error flags are write-one-to-clear. TDF, RDF, MBF,
         * and BBF are controller state flags and are not cleared by an
         * arbitrary MSR write.
         */
        s->regs[LPI2C_MSR / 4] &= ~(v & LPI2C_MSR_W1C_MASK);
        s->regs[LPI2C_MSR / 4] |= LPI2C_MSR_TDF;
        break;

    case LPI2C_MTDR:
    {
        uint32_t cmd = (v & LPI2C_MTDR_CMD_MASK) >> LPI2C_MTDR_CMD_SHIFT;
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
                s->regs[LPI2C_MSR / 4] |= LPI2C_MSR_NDF;
                s->regs[LPI2C_MSR / 4] &= ~LPI2C_MSR_BUSY_MASK;
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
                s->regs[LPI2C_MSR / 4] &= ~LPI2C_MSR_BUSY_MASK;
            } else {
                s->regs[LPI2C_MSR / 4] &= ~LPI2C_MSR_NDF;
            }
            s->regs[LPI2C_MSR / 4] |= LPI2C_MSR_TDF;
            break;

        case LPI2C_MTDR_CMD_RX_DATA:
        {
            /*
             * RX command count is encoded as N - 1.
             */
            unsigned count = data + 1;

            s->rx_len = 0;
            s->rx_pos = 0;

            for (unsigned i = 0; i < count; i++) {
                int byte = i2c_recv(s->bus);
                imx_lpi2c_push_rx(s, byte & 0xff);
            }

            s->regs[LPI2C_MSR / 4] |= LPI2C_MSR_TDF;
            break;
        }

        case LPI2C_MTDR_CMD_STOP:
            i2c_end_transfer(s->bus);
            s->regs[LPI2C_MSR / 4] |= LPI2C_MSR_TDF |
                                      LPI2C_MSR_SDF |
                                      LPI2C_MSR_EPF;
            s->regs[LPI2C_MSR / 4] &= ~LPI2C_MSR_BUSY_MASK;
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
            /* RST is modeled as an immediate, self-clearing reset. */
            imx_lpi2c_master_reset(s);
            break;
        }

        if (v & LPI2C_MCR_RRF) {
            /* RRF resets the receive FIFO and then self-clears. */
            s->rx_len = 0;
            s->rx_pos = 0;
            s->regs[LPI2C_MSR / 4] &= ~LPI2C_MSR_RDF;
        }

        if (v & LPI2C_MCR_RTF) {
            /*
             * MTDR commands are consumed synchronously, so resetting the
             * transmit FIFO leaves it empty and ready.
             */
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

    s->regs[LPI2C_VERID / 4] = 0x00010000;
    s->regs[LPI2C_PARAM / 4] = 0x00000202;

    imx_lpi2c_master_reset(s);
}

static const VMStateDescription vmstate_imx_lpi2c = {
    .name = TYPE_IMX_LPI2C,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, IMXLPI2CState,
                             IMX_LPI2C_MMIO_SIZE / 4),
        VMSTATE_UINT8_ARRAY(rx_fifo, IMXLPI2CState, 16),
        VMSTATE_UINT32(rx_len, IMXLPI2CState),
        VMSTATE_UINT32(rx_pos, IMXLPI2CState),
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

    /*
     * This is the core reusable part: the LPI2C controller exposes a
     * child I2C bus. Board code can attach PMIC, EEPROM, sensors, etc.
     */
    s->bus = i2c_init_bus(DEVICE(obj), "i2c");
}

static void imx_lpi2c_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

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
