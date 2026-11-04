/*
 * NXP i.MX95 CAMERAMIX block control (CAMERA_CSR)
 *
 * Register map, reset values and access types follow the i.MX 95
 * Applications Processor Reference Manual, Rev. 5, chapter 142 (Camera CSR).
 * The CSR only holds configuration for the ISP, ISI and the two MIPI CSI-2
 * receivers; none of the registers has a side effect that the rest of the
 * system observes, so the model stores them.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_IMX95_CAMERA_CSR_H
#define HW_MISC_IMX95_CAMERA_CSR_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_IMX95_CAMERA_CSR "imx95.camera-csr"
OBJECT_DECLARE_SIMPLE_TYPE(Imx95CameraCsrState, IMX95_CAMERA_CSR)

#define IMX95_CAMERA_CSR_SIZE       0x10000
#define IMX95_CAMERA_CSR_NUM_REGS   (0x324 / 4)

struct Imx95CameraCsrState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t regs[IMX95_CAMERA_CSR_NUM_REGS];
};

#endif /* HW_MISC_IMX95_CAMERA_CSR_H */
