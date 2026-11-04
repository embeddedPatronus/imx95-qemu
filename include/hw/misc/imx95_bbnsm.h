/*
 * NXP i.MX95 bbnsm
 *
 * State and type definitions of the model in hw/misc/imx95_bbnsm.c.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_IMX95_BBNSM_H
#define HW_MISC_IMX95_BBNSM_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define IMX95_BBNSM_SIZE 0x10000


#define TYPE_IMX95_BBNSM "imx95.bbnsm"
OBJECT_DECLARE_SIMPLE_TYPE(Imx95BbnsmState, IMX95_BBNSM)

struct Imx95BbnsmState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t regs[IMX95_BBNSM_SIZE / 4];
    uint64_t read_count;
    uint64_t write_count;
};

#endif /* HW_MISC_IMX95_BBNSM_H */
