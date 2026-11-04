/*
 * NXP i.MX95 smmuv3
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_IMX95_SMMUV3_H
#define HW_MISC_IMX95_SMMUV3_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define IMX95_SMMUV3_SIZE 0x00020000
#define IMX95_SMMU_PMCG_NUM         12
#define IMX95_SMMU_PMCG_SIZE        0x1000
#define IMX95_SMMU_PMCG_NUM_CTRS    6



#define TYPE_IMX95_SMMUV3 "imx95.smmuv3"
OBJECT_DECLARE_SIMPLE_TYPE(Imx95Smmuv3State, IMX95_SMMUV3)

struct Imx95Smmuv3State {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t gbpa;
};

#define TYPE_IMX95_SMMU_PMCG "imx95.smmu-pmcg"
OBJECT_DECLARE_SIMPLE_TYPE(Imx95SmmuPmcgState, IMX95_SMMU_PMCG)

struct Imx95SmmuPmcgState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t evcntr[IMX95_SMMU_PMCG_NUM_CTRS];
    uint32_t evtyper[IMX95_SMMU_PMCG_NUM_CTRS];
    uint32_t smr[IMX95_SMMU_PMCG_NUM_CTRS];
    uint32_t cnten;
    uint32_t inten;
    uint32_t ovs;
    uint32_t cr;
    uint32_t irq_ctrl;
    uint32_t irq_cfg0[2];
    uint32_t irq_cfg1;
    uint32_t irq_cfg2;
};

#endif /* HW_MISC_IMX95_SMMUV3_H */
