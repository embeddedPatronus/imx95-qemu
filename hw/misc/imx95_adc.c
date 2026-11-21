/*
 * NXP i.MX95 ADC model
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "hw/core/irq.h"
#include "hw/misc/imx95_adc.h"
#include "migration/vmstate.h"

#define IMX95_ADC_MCR      0x000
#define IMX95_ADC_MSR      0x004
#define IMX95_ADC_ISR      0x010
#define IMX95_ADC_IMR      0x020
#define IMX95_ADC_CIMR0    0x024
#define IMX95_ADC_NCMR0    0x0a4
#define IMX95_ADC_PCDR0    0x100
#define IMX95_ADC_CALSTAT  0x39c
#define IMX95_ADC_CALCFG0  0x3a0

#define IMX95_ADC_NUM_CHANNELS 8

#define IMX95_ADC_MCR_NSTART   BIT(24)
#define IMX95_ADC_MCR_CALSTART BIT(14)
#define IMX95_ADC_MCR_PWDN     BIT(0)

#define IMX95_ADC_MSR_CALFAIL  BIT(30)
#define IMX95_ADC_MSR_CALBUSY  BIT(29)
#define IMX95_ADC_MSR_STATUS_MASK MAKE_64BIT_MASK(0, 3)
#define IMX95_ADC_MSR_STATUS_IDLE 0U
#define IMX95_ADC_MSR_STATUS_POWER_DOWN 1U

#define IMX95_ADC_ISR_ECH BIT(0)
#define IMX95_ADC_ISR_EOC BIT(1)
#define IMX95_ADC_ISR_EOC_ECH \
    (IMX95_ADC_ISR_EOC | IMX95_ADC_ISR_ECH)

#define IMX95_ADC_IMR_ECH BIT(0)
#define IMX95_ADC_IMR_EOC BIT(1)

static unsigned int imx95_adc_reg_index(hwaddr offset)
{
    return offset / sizeof(uint32_t);
}

static void imx95_adc_update_power_state(Imx95AdcState *s)
{
    uint32_t *msr = &s->regs[imx95_adc_reg_index(IMX95_ADC_MSR)];

    *msr &= ~IMX95_ADC_MSR_STATUS_MASK;
    if (s->regs[imx95_adc_reg_index(IMX95_ADC_MCR)] &
        IMX95_ADC_MCR_PWDN) {
        *msr |= IMX95_ADC_MSR_STATUS_POWER_DOWN;
    } else {
        *msr |= IMX95_ADC_MSR_STATUS_IDLE;
    }
}

static void imx95_adc_complete_calibration(Imx95AdcState *s)
{
    uint32_t *mcr = &s->regs[imx95_adc_reg_index(IMX95_ADC_MCR)];
    uint32_t *msr = &s->regs[imx95_adc_reg_index(IMX95_ADC_MSR)];

    /* Calibration always succeeds */
    *msr &= ~(IMX95_ADC_MSR_CALBUSY | IMX95_ADC_MSR_CALFAIL);
    s->regs[imx95_adc_reg_index(IMX95_ADC_CALSTAT)] = 0;
    *mcr &= ~IMX95_ADC_MCR_CALSTART;
}

static void imx95_adc_complete_conversion(Imx95AdcState *s)
{
    uint32_t *mcr = &s->regs[imx95_adc_reg_index(IMX95_ADC_MCR)];
    uint32_t imr = s->regs[imx95_adc_reg_index(IMX95_ADC_IMR)];
    uint32_t channels =
        s->regs[imx95_adc_reg_index(IMX95_ADC_NCMR0)] & UINT8_MAX;
    unsigned int channel;

    /* Conversions are instant and every enabled channel reads 0 */
    for (channel = 0; channel < IMX95_ADC_NUM_CHANNELS; channel++) {
        if (channels & BIT(channel)) {
            s->regs[imx95_adc_reg_index(IMX95_ADC_PCDR0) + channel] = 0;
        }
    }

    s->regs[imx95_adc_reg_index(IMX95_ADC_ISR)] |=
        IMX95_ADC_ISR_EOC_ECH;
    *mcr &= ~IMX95_ADC_MCR_NSTART;

    if (imr & (IMX95_ADC_IMR_EOC | IMX95_ADC_IMR_ECH)) {
        qemu_irq_pulse(s->irq);
    }
}

static uint64_t imx95_adc_read(void *opaque, hwaddr offset,
                               unsigned int size)
{
    Imx95AdcState *s = opaque;

    if (size != sizeof(uint32_t) ||
        !QEMU_IS_ALIGNED(offset, sizeof(uint32_t)) ||
        offset >= IMX95_ADC_SIZE) {
        return 0;
    }

    return s->regs[imx95_adc_reg_index(offset)];
}

static void imx95_adc_write(void *opaque, hwaddr offset,
                            uint64_t value, unsigned int size)
{
    Imx95AdcState *s = opaque;
    uint32_t old_value;
    uint32_t new_value = value;

    if (size != sizeof(uint32_t) ||
        !QEMU_IS_ALIGNED(offset, sizeof(uint32_t)) ||
        offset >= IMX95_ADC_SIZE) {
        return;
    }

    old_value = s->regs[imx95_adc_reg_index(offset)];

    switch (offset) {
    case IMX95_ADC_MSR:
    case IMX95_ADC_CALSTAT:
        return;
    case IMX95_ADC_ISR:
        s->regs[imx95_adc_reg_index(IMX95_ADC_ISR)] &= ~new_value;
        return;
    case IMX95_ADC_MCR:
        s->regs[imx95_adc_reg_index(IMX95_ADC_MCR)] = new_value;
        imx95_adc_update_power_state(s);
        if ((new_value & IMX95_ADC_MCR_CALSTART) &&
            !(old_value & IMX95_ADC_MCR_CALSTART)) {
            imx95_adc_complete_calibration(s);
        }
        if ((new_value & IMX95_ADC_MCR_NSTART) &&
            !(old_value & IMX95_ADC_MCR_NSTART)) {
            imx95_adc_complete_conversion(s);
        }
        break;
    default:
        s->regs[imx95_adc_reg_index(offset)] = new_value;
        break;
    }
}

static const MemoryRegionOps imx95_adc_ops = {
    .read = imx95_adc_read,
    .write = imx95_adc_write,
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

static void imx95_adc_reset(DeviceState *dev)
{
    Imx95AdcState *s = IMX95_ADC(dev);

    memset(s->regs, 0, sizeof(s->regs));
    s->regs[imx95_adc_reg_index(IMX95_ADC_MCR)] = IMX95_ADC_MCR_PWDN;
    s->regs[imx95_adc_reg_index(IMX95_ADC_MSR)] =
        IMX95_ADC_MSR_STATUS_POWER_DOWN;
    qemu_irq_lower(s->irq);
}

static void imx95_adc_init(Object *obj)
{
    Imx95AdcState *s = IMX95_ADC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_adc_ops, s,
                          TYPE_IMX95_ADC, IMX95_ADC_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static const VMStateDescription vmstate_imx95_adc = {
    .name = TYPE_IMX95_ADC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, Imx95AdcState,
                             IMX95_ADC_REG_COUNT),
        VMSTATE_END_OF_LIST()
    },
};

static void imx95_adc_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, imx95_adc_reset);
    dc->vmsd = &vmstate_imx95_adc;
    dc->desc = "i.MX95 ADC";
}

static const TypeInfo imx95_adc_types[] = {
    {
        .name = TYPE_IMX95_ADC,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Imx95AdcState),
        .instance_init = imx95_adc_init,
        .class_init = imx95_adc_class_init,
    },
};

DEFINE_TYPES(imx95_adc_types)

