/*
 * NXP i.MX95 DDR controller
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_IMX95_DDRC_H
#define HW_MISC_IMX95_DDRC_H

#include "hw/core/sysbus.h"

#define TYPE_IMX95_DDRC "imx95.ddrc"
OBJECT_DECLARE_SIMPLE_TYPE(Imx95DdrcState, IMX95_DDRC)

#define IMX95_DDRC_SIZE      0x20000
#define IMX95_DDRC_REG_COUNT (IMX95_DDRC_SIZE / sizeof(uint32_t))
#define IMX95_DDRC_NUM_RANKS 2

struct Imx95DdrcState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t regs[IMX95_DDRC_REG_COUNT];
    uint64_t dram_base;
    uint64_t dram_size;
    uint64_t rank0_size;
    uint8_t rank_count;
    uint8_t row_bits;
    uint8_t column_bits;
    uint8_t bank_group_bits;
    bool phy_init_complete;
    bool memory_enabled;
};

#endif /* HW_MISC_IMX95_DDRC_H */

