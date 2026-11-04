/*
 * NXP i.MX95 CAMERAMIX NoC
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_IMX95_CAMERA_NOC_H
#define HW_MISC_IMX95_CAMERA_NOC_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_IMX95_CAMERA_NOC "imx95.camera-noc"
OBJECT_DECLARE_SIMPLE_TYPE(Imx95CameraNocState, IMX95_CAMERA_NOC)

#define IMX95_CAMERA_NOC_SIZE           0x10000
#define IMX95_CAMERA_NOC_NUM_QOSGEN     5

struct Imx95CameraNocState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t qosgen[IMX95_CAMERA_NOC_NUM_QOSGEN];
};

#endif /* HW_MISC_IMX95_CAMERA_NOC_H */
