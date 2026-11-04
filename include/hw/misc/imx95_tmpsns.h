/*
 * NXP i.MX95 tmpsns
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_IMX95_TMPSNS_H
#define HW_MISC_IMX95_TMPSNS_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define IMX95_TMPSNS_SIZE     0x1000



#define TYPE_IMX95_TMPSNS "imx95.tmpsns"
OBJECT_DECLARE_SIMPLE_TYPE(Imx95TmpsnsState, IMX95_TMPSNS)

struct Imx95TmpsnsState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t regs[IMX95_TMPSNS_SIZE / sizeof(uint32_t)];
};

#endif /* HW_MISC_IMX95_TMPSNS_H */
