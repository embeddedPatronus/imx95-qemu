/*
 * NXP i.MX95 blk ctrl s aonmix
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_IMX95_BLK_CTRL_S_AONMIX_H
#define HW_MISC_IMX95_BLK_CTRL_S_AONMIX_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define IMX95_ELE_MU3_CA55_MASK_REG   0x040
#define IMX95_ELE_MU3_CA55_MASK_BIT   BIT(24) /* GIC SPI 24 */
#define IMX95_BLK_CTRL_S_AONMIX_SIZE 0x10000

#define BLK_CTRL_CA55_CPUWAIT        0x160
#define BLK_CTRL_CA55_RVBARADDR0_L   0x164
#define BLK_CTRL_CA55_RVBARADDR0_H   0x168
#define BLK_CTRL_CA55_RVBARADDR1_L   0x16C
#define BLK_CTRL_CA55_RVBARADDR1_H   0x170
#define BLK_CTRL_CA55_RVBARADDR2_L   0x174
#define BLK_CTRL_CA55_RVBARADDR2_H   0x178
#define BLK_CTRL_CA55_RVBARADDR3_L   0x17C
#define BLK_CTRL_CA55_RVBARADDR3_H   0x180
#define BLK_CTRL_CA55_RVBARADDR4_L   0x184
#define BLK_CTRL_CA55_RVBARADDR4_H   0x188
#define BLK_CTRL_CA55_RVBARADDR5_L   0x18C
#define BLK_CTRL_CA55_RVBARADDR5_H   0x190
#define BLK_CTRL_CA55_MODE           0x1AC
#define IMX95_ELE_MU3_CA55_MASK_BIT   BIT(24) /* GIC SPI 24 */
#define IMX95_ELE_MU3_CA55_MASK_REG   0x040
#define IMX95_BLK_CTRL_NUM_A55          6

#define TYPE_IMX95_BLK_CTRL_S_AONMIX "imx95.blk-ctrl-s-aonmix"
OBJECT_DECLARE_SIMPLE_TYPE(Imx95BlkCtrlSAonmixState, IMX95_BLK_CTRL_S_AONMIX)

typedef struct Imx95A55Ctx {
    Imx95BlkCtrlSAonmixState *s;
    unsigned int idx;
} Imx95A55Ctx;

struct Imx95BlkCtrlSAonmixState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t regs[IMX95_BLK_CTRL_S_AONMIX_SIZE / 4];

    qemu_irq ele_mu3_irq_mask;

    bool a55_spl_loaded;
    uint64_t a55_spl_entry;

    bool a55_released;
    bool a55_secondary_released[IMX95_BLK_CTRL_NUM_A55];

    Imx95A55Ctx a55_ctx[IMX95_BLK_CTRL_NUM_A55];
    bool a55_want_on[IMX95_BLK_CTRL_NUM_A55];
    uint64_t a55_want_entry[IMX95_BLK_CTRL_NUM_A55];
    uint32_t a55_want_gen[IMX95_BLK_CTRL_NUM_A55]; 
    uint32_t a55_run_gen[IMX95_BLK_CTRL_NUM_A55];
    bool a55_op_pending[IMX95_BLK_CTRL_NUM_A55];

    bool a55_pwrdn[IMX95_BLK_CTRL_NUM_A55];
    bool a55_gic_wake[IMX95_BLK_CTRL_NUM_A55];
};

#endif /* HW_MISC_IMX95_BLK_CTRL_S_AONMIX_H */
