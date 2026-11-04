/*
 * NXP i.MX95 ccm
 *
 * State and type definitions of the model in hw/misc/imx95_ccm.c.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_IMX95_CCM_H
#define HW_MISC_IMX95_CCM_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define IMX95_CCM_SIZE  0x10000


#define TYPE_IMX95_CCM "imx95.ccm"
OBJECT_DECLARE_SIMPLE_TYPE(Imx95CcmState, IMX95_CCM)

struct Imx95CcmState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t regs[IMX95_CCM_SIZE / 4];
};

#endif /* HW_MISC_IMX95_CCM_H */
