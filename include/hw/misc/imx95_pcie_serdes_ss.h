/*
 * NXP i.MX95 PCIe SerDes subsystem
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
    qemu_irq ltssm_en;
    MemoryRegion msi_doorbell;
    uint32_t devid_prefix;
    uint64_t msi_target;
    uint32_t regs[IMX95_PCIE_SERDES_SS_NUM_REGS];
    uint32_t lut[IMX95_PCIE_SERDES_SS_LUT_ENTRIES][2];
};

#endif /* HW_MISC_IMX95_PCIE_SERDES_SS_H */
