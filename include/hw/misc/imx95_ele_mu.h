/*
 * NXP i.MX95 ele mu
 *
 * State and type definitions of the model in hw/misc/imx95_ele_mu.c.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_IMX95_ELE_MU_H
#define HW_MISC_IMX95_ELE_MU_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define IMX95_ELE_MU_SIZE 0x80000

#define ELE_MSG_VER      0x06
#define ELE_MSG_TAG_RESP 0xe1
#define ELE_ENABLE_AUX_REQ 0xd4
#define ELE_RELEASE_RDC_REQ 0xc4
#define ELE_GET_INFO_REQ 0xda
#define ELE_GET_FW_VERSION_REQ 0x9d
#define ELE_GET_V2X_FW_STATE_REQ 0xb2
#define ELE_TRNG_STATE_REQ 0xa4
#define ELE_GET_RANDOM_REQ 0xcd
#define ELE_TRNG_CSAL_STATE_READY 0x00000203U
#define ELE_TRNG_STATE_RESP_WORDS 3U
#define ELE_GET_RANDOM_RESP_WORDS 2U
#define ELE_GET_FW_VERSION_RESP_WORDS 4U
#define ELE_GET_V2X_FW_STATE_RESP_WORDS 4U
#define ELE_GET_RANDOM_MAX_BYTES 16U
#define ELE_FW_VERSION_STUB 0x00000000U
#define V2X_FW_AUTH_NORM_COMPLETE 0x00000013U

#define ELE_GET_INFO_SOC_IMX95_REV10  0xa0000000U
#define ELE_GET_INFO_LC_OEM_OPEN      0x00000020U
#define IMX95_ELE_MU3_BLOCK         5U

#define TYPE_IMX95_ELE_MU "imx95.ele-mu"
OBJECT_DECLARE_SIMPLE_TYPE(Imx95EleMuState, IMX95_ELE_MU)

struct Imx95EleMuState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t regs[IMX95_ELE_MU_SIZE / 4];

    uint32_t tx_words[8];
    uint32_t tx_count;
    uint32_t tx_expected;

    uint32_t rx_words[8];
    uint32_t rx_count;
    uint32_t rx_pos;
    unsigned int active_block;

    qemu_irq mu3_irq;
    bool ca55_masked;
    bool mu3_irq_level;
};

#endif /* HW_MISC_IMX95_ELE_MU_H */
