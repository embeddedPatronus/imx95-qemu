/*
 * NXP i.MX95 SRC GEN
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_IMX95_SRC_GEN_H
#define HW_MISC_IMX95_SRC_GEN_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define IMX95_SRC_GEN_SIZE               0x10000
#define IMX95_SRC_GEN_MAX_A55            6

#define TYPE_IMX95_SRC_GEN "imx95.src-gen"
OBJECT_DECLARE_SIMPLE_TYPE(Imx95SrcGenState, IMX95_SRC_GEN)

struct Imx95SrcGenState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t regs[IMX95_SRC_GEN_SIZE / sizeof(uint32_t)];

    qemu_irq a55_hold[IMX95_SRC_GEN_MAX_A55 + 1];
    bool a55_hold_state[IMX95_SRC_GEN_MAX_A55 + 1];

    uint32_t a55_core0_slice;
    uint32_t a55_platform_slice;
    uint32_t num_a55;
};

#endif /* HW_MISC_IMX95_SRC_GEN_H */
