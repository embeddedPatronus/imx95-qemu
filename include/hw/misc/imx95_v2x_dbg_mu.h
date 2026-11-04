/*
 * NXP i.MX95 v2x dbg mu
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_IMX95_V2X_DBG_MU_H
#define HW_MISC_IMX95_V2X_DBG_MU_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define IMX95_V2X_DBG_MU_SIZE              0x00010000

#define V2X_DEBUG_MU_MSG_VERSION            0x02U
#define V2X_DEBUG_MU_MSG_COMMAND_TAG        0x17U
#define V2X_DEBUG_MU_MSG_RESPONSE_TAG       0xe1U
#define V2X_START_RNG_REQ                   0x0eU
#define V2X_START_RNG_REQ_WORDS             1U
#define V2X_START_RNG_RESPONSE_WORDS        3U
#define V2X_START_RNG_STATE_READY           0x00000000U
#define V2X_SUCCESS_IND                     ELE_SUCCESS_IND

#define TYPE_IMX95_V2X_DBG_MU "imx95.v2x-dbg-mu"
OBJECT_DECLARE_SIMPLE_TYPE(Imx95V2xDbgMuState, IMX95_V2X_DBG_MU)

struct Imx95V2xDbgMuState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t regs[IMX95_V2X_DBG_MU_SIZE / sizeof(uint32_t)];

    uint32_t tx_words[8];
    uint32_t tx_count;
    uint32_t tx_expected;

    uint32_t rx_words[8];
    uint32_t rx_count;
    uint32_t rx_pos;

    qemu_irq irq;
    bool irq_level;
};

#endif /* HW_MISC_IMX95_V2X_DBG_MU_H */
