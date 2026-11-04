/*
 * NXP i.MX95 Messaging Unit
 *
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_MISC_IMX95_MU_H
#define HW_MISC_IMX95_MU_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_IMX95_MU "imx95-mu"
OBJECT_DECLARE_SIMPLE_TYPE(IMX95MUState, IMX95_MU)

#define IMX95_MU_NUM_CHANNELS 4
#define IMX95_MU_MMIO_SIZE    0x1000

struct IMX95MUState {
    SysBusDevice parent_obj;

    MemoryRegion iomem[2];
    qemu_irq irq[2];

    uint32_t cr[2];
    uint32_t ccr0[2];
    uint32_t cier0[2];
    uint32_t cssr0[2];
    uint32_t fcr[2];
    uint32_t flags;
    uint32_t gier[2];
    uint32_t gp_pending[2];
    uint32_t tcr[2];
    uint32_t rcr[2];
    uint32_t tx[2][IMX95_MU_NUM_CHANNELS];
    uint32_t rx[2][IMX95_MU_NUM_CHANNELS];
    uint8_t tx_empty[2];
    uint8_t rx_full[2];
    bool irq_level[2];     /* last level driven on irq[] (derived state) */
};

#endif
