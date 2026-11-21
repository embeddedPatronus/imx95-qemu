/*
 * NXP i.MX95 SC MU
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_IMX95_SC_MU_H
#define HW_MISC_IMX95_SC_MU_H

#include "hw/core/sysbus.h"

#define IMX95_SCMI_CLOCK_ID_MAX 0x100
#define IMX95_SCMI_SMT_SIZE     0x400

/* i.MX8 MU register offsets. */
#define IMX8_MU_TR0 0x00
#define IMX8_MU_RR0 0x10
#define IMX8_MU_SR  0x20
#define IMX8_MU_CR  0x24

#define IMX8_MU_SR_TE_ALL (0xfU << 20)
#define IMX8_MU_SR_RF_ALL (0xfU << 24)

/* SC RPC constants. */
#define SC_RPC_VERSION    1U
#define SC_RPC_SVC_RETURN 1U
#define SC_RPC_MAX_MSG    8U

#define IMX95_SC_MU_NUM_REGS 4
#define IMX95_SC_MU_SIZE     0x10000U

#define TYPE_IMX95_SC_MU "imx95.sc-mu"
OBJECT_DECLARE_SIMPLE_TYPE(Imx95ScMuState, IMX95_SC_MU)

struct Imx95ScMuState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t tr[IMX95_SC_MU_NUM_REGS];
    uint32_t rr[IMX95_SC_MU_NUM_REGS];
    uint32_t tx_count;
    uint32_t tx_size;
    bool rx_ready;
    uint32_t rx_count;
    uint32_t rx_size;
};

#endif /* HW_MISC_IMX95_SC_MU_H */

