/*
 * NXP i.MX95 PCIe SerDes subsystem (SerDes_SS) registers
 *
 * Register map, reset values and access types follow the i.MX 95
 * Applications Processor Reference Manual, Rev. 5, section 160.3
 * (SerDes_SS register descriptions). One instance per PCIe controller:
 * 0x4c340000 (PCIe1 in the RM) and 0x4c3c0000 (PCIe2 in the RM).
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_IMX95_PCIE_SERDES_SS_H
#define HW_MISC_IMX95_PCIE_SERDES_SS_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_IMX95_PCIE_SERDES_SS "imx95.pcie-serdes-ss"
OBJECT_DECLARE_SIMPLE_TYPE(Imx95PcieSerdesSsState, IMX95_PCIE_SERDES_SS)

#define IMX95_PCIE_SERDES_SS_SIZE       0x4000
#define IMX95_PCIE_SERDES_SS_NUM_REGS   3077
#define IMX95_PCIE_SERDES_SS_LUT_ENTRIES 32

struct Imx95PcieSerdesSsState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t regs[IMX95_PCIE_SERDES_SS_NUM_REGS];
    /* PE0_LUT: {DATA1, DATA2} per entry, accessed through PE0_LUT_ACSCTRL */
    uint32_t lut[IMX95_PCIE_SERDES_SS_LUT_ENTRIES][2];
};

#endif /* HW_MISC_IMX95_PCIE_SERDES_SS_H */
