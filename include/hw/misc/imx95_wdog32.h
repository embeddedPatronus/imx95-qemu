/*
 * NXP i.MX95 wdog32
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_IMX95_WDOG32_H
#define HW_MISC_IMX95_WDOG32_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define IMX95_WDOG_SIZE  0x10000


#define TYPE_IMX95_WDOG32 "imx95.wdog32"
OBJECT_DECLARE_SIMPLE_TYPE(Imx95Wdog32State, IMX95_WDOG32)

struct Imx95Wdog32State {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t cs;
    uint32_t cnt;
    uint32_t toval;
    uint32_t win;
    bool unlock_seen_first;
};

#endif /* HW_MISC_IMX95_WDOG32_H */
