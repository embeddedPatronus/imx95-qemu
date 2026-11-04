/*
 * NXP i.MX95 CAMERAMIX NoC (GPV) - minimal model
 *
 * The CAMERAMIX interconnect registers at 0x4aff0000 are not described in
 * the i.MX 95 Reference Manual, Rev. 5; the System Manager header only gives
 * the base address of every QoS generator inside it, e.g.
 * CAMERA__GPV__ISI_WR_U_I_MAIN_QOSGENERATOR_BASE (0x4AFF0C80). The System
 * Manager firmware programs one register, at offset 0x18 of the five
 * ISI/ISP write-path QoS generators, and reads it back. This model provides
 * exactly those five registers (plain read/write words, reset value 0) and
 * reports every other offset of the window as undefined.
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
