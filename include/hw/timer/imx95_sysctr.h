/*
 * NXP i.MX95 System Counter (SYS_CTR)
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_TIMER_IMX95_SYSCTR_H
#define HW_TIMER_IMX95_SYSCTR_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_IMX95_SYSCTR "imx95.sysctr"
OBJECT_DECLARE_SIMPLE_TYPE(Imx95SysctrState, IMX95_SYSCTR)

#define IMX95_SYSCTR_FRAME_SIZE     0x10000
#define IMX95_SYSCTR_NUM_COMPARE    2

struct Imx95SysctrState {
    SysBusDevice parent_obj;

    MemoryRegion iomem[3];          /* control, compare, read */
    qemu_irq irq[IMX95_SYSCTR_NUM_COMPARE];
    QEMUTimer *timer;

    uint32_t cntcr;
    bool alt_freq;                  /* CNTSR.FCA1: alternate frequency selected */
    uint64_t cnt_base;              /* counter value at t_base */
    int64_t t_base;                 /* virtual-clock time (ns) of cnt_base */
    uint64_t cmpcv[IMX95_SYSCTR_NUM_COMPARE];
    uint32_t cmpcr[IMX95_SYSCTR_NUM_COMPARE];
};

#endif /* HW_TIMER_IMX95_SYSCTR_H */
