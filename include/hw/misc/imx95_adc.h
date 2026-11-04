/*
 * NXP i.MX95 adc
 *
 * State and type definitions of the model in hw/misc/imx95_adc.c.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_IMX95_ADC_H
#define HW_MISC_IMX95_ADC_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define IMX95_ADC_SIZE 0x00010000

#define TYPE_IMX95_ADC "imx95.adc"
OBJECT_DECLARE_SIMPLE_TYPE(Imx95AdcState, IMX95_ADC)

struct Imx95AdcState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t regs[IMX95_ADC_SIZE / sizeof(uint32_t)];
    qemu_irq irq;
};

#endif /* HW_MISC_IMX95_ADC_H */
