/*
 * NXP i.MX95 fractpll
 *
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_IMX95_FRACTPLL_H
#define HW_MISC_IMX95_FRACTPLL_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define IMX95_FRACTPLL_SIZE 0x1000


#define TYPE_IMX95_FRACTPLL "imx95.fractpll"
OBJECT_DECLARE_SIMPLE_TYPE(Imx95FractpllState, IMX95_FRACTPLL)

struct Imx95FractpllState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t regs[IMX95_FRACTPLL_SIZE / 4];
};

#endif /* HW_MISC_IMX95_FRACTPLL_H */
