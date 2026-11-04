/*
 * Minimal i.MX95 ADC model
 *
 *
 * The i.MX95 device tree exposes an nxp,imx93-adc compatible block at
 * 0x44530000.  The Linux driver performs these operations during probe:
 *
 *   - set MCR.PWDN and wait for MSR.ADCSTATUS == POWER_DOWN
 *   - clear MCR.PWDN and expect the ADC to return to IDLE
 *   - start calibration and wait for calibration to complete
 *
 * Runtime conversion sets MCR.NSTART and waits for an ADC interrupt.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "hw/core/qdev-properties.h"
#include "hw/misc/imx95_adc.h"
#include "migration/vmstate.h"
#include "hw/core/irq.h"
#include "system/memory.h"

#define IMX95_ADC_IRQ  199

#define IMX95_ADC_MCR      0x000
#define IMX95_ADC_MSR      0x004
#define IMX95_ADC_ISR      0x010
#define IMX95_ADC_IMR      0x020
#define IMX95_ADC_CIMR0    0x024
#define IMX95_ADC_NCMR0    0x0a4
#define IMX95_ADC_PCDR0    0x100
#define IMX95_ADC_CALSTAT  0x39c
#define IMX95_ADC_CALCFG0  0x3a0
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

#define IMX95_ADC_PCDR_CDATA_MASK MAKE_64BIT_MASK(0, 12)


static void imx95_adc_update_power_state(Imx95AdcState *s)
{
    uint32_t msr = s->regs[IMX95_ADC_MSR / sizeof(uint32_t)];

    msr &= ~IMX95_ADC_MSR_STATUS_MASK;

    if (s->regs[IMX95_ADC_MCR / sizeof(uint32_t)] &
        IMX95_ADC_MCR_PWDN) {
        msr |= IMX95_ADC_MSR_STATUS_POWER_DOWN;
    } else {
        msr |= IMX95_ADC_MSR_STATUS_IDLE;
    }

    s->regs[IMX95_ADC_MSR / sizeof(uint32_t)] = msr;
}

static void imx95_adc_complete_calibration(Imx95AdcState *s)
{
    uint32_t *mcr = &s->regs[IMX95_ADC_MCR / sizeof(uint32_t)];
    uint32_t *msr = &s->regs[IMX95_ADC_MSR / sizeof(uint32_t)];

    *msr &= ~(IMX95_ADC_MSR_CALBUSY | IMX95_ADC_MSR_CALFAIL);
    s->regs[IMX95_ADC_CALSTAT / sizeof(uint32_t)] = 0;
    *mcr &= ~IMX95_ADC_MCR_CALSTART;

    qemu_log_mask(LOG_GUEST_ERROR,
                  "imx95.adc: calibration completed\n");
}

static void imx95_adc_complete_conversion(Imx95AdcState *s)
{
    uint32_t *mcr = &s->regs[IMX95_ADC_MCR / sizeof(uint32_t)];
    uint32_t imr = s->regs[IMX95_ADC_IMR / sizeof(uint32_t)];
    uint32_t channels =
        s->regs[IMX95_ADC_NCMR0 / sizeof(uint32_t)] & UINT8_MAX;
    unsigned int channel;

    /*
     * Return a deterministic value of zero for each selected channel.
     * PCDR data is limited to 12 bits by the Linux driver.
     */
    for (channel = 0; channel < 8; channel++) {
        if (channels & BIT(channel)) {
            s->regs[(IMX95_ADC_PCDR0 / sizeof(uint32_t)) + channel] =
                0 & IMX95_ADC_PCDR_CDATA_MASK;
        }
    }

    s->regs[IMX95_ADC_ISR / sizeof(uint32_t)] |=
        IMX95_ADC_ISR_EOC_ECH;

    /*
     * NSTART is hardware-cleared when the normal conversion completes.
     */
    *mcr &= ~IMX95_ADC_MCR_NSTART;

    if (imr & (IMX95_ADC_IMR_EOC | IMX95_ADC_IMR_ECH)) {
        qemu_irq_pulse(s->irq);
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                  "imx95.adc: conversion completed channels=0x%02x "
                  "imr=0x%08x\n",
                  channels, imr);
}

static uint64_t imx95_adc_read(void *opaque,
                               hwaddr offset,
                               unsigned int size)
{
    Imx95AdcState *s = opaque;
    uint32_t value;

    if (size != sizeof(uint32_t) ||
        (offset & 3) != 0 ||
        offset >= IMX95_ADC_SIZE) {
        return 0;
    }

    value = s->regs[offset / sizeof(uint32_t)];

    if (offset == IMX95_ADC_MCR ||
        offset == IMX95_ADC_MSR ||
        offset == IMX95_ADC_ISR ||
        offset == IMX95_ADC_IMR ||
        offset == IMX95_ADC_CALSTAT) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.adc: read offset=0x%03" HWADDR_PRIx
                      " value=0x%08x\n",
                      offset, value);
    }

    return value;
}

static void imx95_adc_write(void *opaque,
                            hwaddr offset,
                            uint64_t value,
                            unsigned int size)
{
    Imx95AdcState *s = opaque;
    uint32_t old_value;
    uint32_t new_value = value;

    if (size != sizeof(uint32_t) ||
        (offset & 3) != 0 ||
        offset >= IMX95_ADC_SIZE) {
        return;
    }

    old_value = s->regs[offset / sizeof(uint32_t)];

    switch (offset) {
    case IMX95_ADC_MSR:
    case IMX95_ADC_CALSTAT:
        /* Hardware-owned status registers. */
        return;

    case IMX95_ADC_ISR:
        /* Interrupt status is write-one-to-clear. */
        s->regs[IMX95_ADC_ISR / sizeof(uint32_t)] &= ~new_value;
        return;

    case IMX95_ADC_MCR:
        s->regs[IMX95_ADC_MCR / sizeof(uint32_t)] = new_value;
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
        s->regs[offset / sizeof(uint32_t)] = new_value;
        break;
    }

    if (offset == IMX95_ADC_MCR ||
        offset == IMX95_ADC_IMR ||
        offset == IMX95_ADC_CIMR0 ||
        offset == IMX95_ADC_NCMR0 ||
        offset == IMX95_ADC_CALCFG0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.adc: write offset=0x%03" HWADDR_PRIx
                      " old=0x%08x value=0x%08x\n",
                      offset, old_value, new_value);
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


    /*
     * Hardware reset state used by the Linux probe:
     *
     *   MCR.PWDN = 1
     *   MSR.ADCSTATUS = POWER_DOWN
     *   calibration status = successful
     */
    s->regs[IMX95_ADC_MCR / sizeof(uint32_t)] = IMX95_ADC_MCR_PWDN;
    s->regs[IMX95_ADC_MSR / sizeof(uint32_t)] =
        IMX95_ADC_MSR_STATUS_POWER_DOWN;
    s->regs[IMX95_ADC_CALSTAT / sizeof(uint32_t)] = 0;
}

static void imx95_adc_init(Object *obj)
{
    Imx95AdcState *s = IMX95_ADC(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_adc_ops, s,
                          TYPE_IMX95_ADC, IMX95_ADC_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static const VMStateDescription vmstate_imx95_adc = {
    .name = TYPE_IMX95_ADC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, Imx95AdcState, IMX95_ADC_SIZE / sizeof(uint32_t)),
        VMSTATE_END_OF_LIST()
    }
};

static void imx95_adc_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, imx95_adc_reset);
    dc->vmsd = &vmstate_imx95_adc;
    dc->desc = "i.MX95 adc";
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
