/*
 * NXP i.MX95 HSIOMIX block control
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_IMX95_HSIO_BLK_CTRL_H
#define HW_MISC_IMX95_HSIO_BLK_CTRL_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_IMX95_HSIO_BLK_CTRL "imx95.hsio-blk-ctrl"
OBJECT_DECLARE_SIMPLE_TYPE(Imx95HsioBlkCtrlState, IMX95_HSIO_BLK_CTRL)

#define IMX95_HSIO_BLK_CTRL_SIZE        0x10000
#define IMX95_HSIO_BLK_CTRL_NUM_REGS    (0xc4 / 4)

struct Imx95HsioBlkCtrlState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t regs[IMX95_HSIO_BLK_CTRL_NUM_REGS];
};

#endif /* HW_MISC_IMX95_HSIO_BLK_CTRL_H */
