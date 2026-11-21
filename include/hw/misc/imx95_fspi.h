/*
 * NXP i.MX95 FlexSPI
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_IMX95_FSPI_H
#define HW_MISC_IMX95_FSPI_H

#include "hw/core/sysbus.h"

#define IMX95_FSPI_REG_SIZE  0x00010000
#define IMX95_FSPI_REG_COUNT (IMX95_FSPI_REG_SIZE / sizeof(uint32_t))

#define TYPE_IMX95_FSPI "imx95.fspi"
OBJECT_DECLARE_SIMPLE_TYPE(Imx95FspiState, IMX95_FSPI)

struct Imx95FspiState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t regs[IMX95_FSPI_REG_COUNT];
};

#endif /* HW_MISC_IMX95_FSPI_H */

