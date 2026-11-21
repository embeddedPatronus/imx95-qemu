/*
 * NXP i.MX95 GPIO
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_IMX95_GPIO_H
#define HW_MISC_IMX95_GPIO_H

#include "hw/core/sysbus.h"

#define IMX95_GPIO_SIZE      0x10000
#define IMX95_GPIO_REG_COUNT (IMX95_GPIO_SIZE / sizeof(uint32_t))

#define TYPE_IMX95_GPIO "imx95.gpio"
OBJECT_DECLARE_SIMPLE_TYPE(Imx95GpioState, IMX95_GPIO)

struct Imx95GpioState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t regs[IMX95_GPIO_REG_COUNT];
};

#endif /* HW_MISC_IMX95_GPIO_H */

