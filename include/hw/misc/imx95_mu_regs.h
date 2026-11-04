/*
 * NXP i.MX95 Messaging Unit register layout shared by the ELE and V2X MU models
 *
 * Offsets of the registers that the ELE and V2X message units share.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_IMX95_MU_REGS_H
#define HW_MISC_IMX95_MU_REGS_H

#define IMX95_MU_BLOCK_SIZE 0x10000
#define IMX95_MU_TCR_OFFSET 0x120
#define IMX95_MU_PAR_OFFSET 0x004
#define IMX95_MU_TSR_OFFSET 0x124
#define IMX95_MU_RCR_OFFSET 0x128
#define IMX95_MU_RSR_OFFSET 0x12c
#define IMX95_MU_TR_OFFSET  0x200
#define IMX95_MU_RR_OFFSET  0x280
#define IMX95_MU_PAR_4TR_4RR 0x00000404U
#define IMX95_MU_TX_READY_BITS 0x0000000f
#define IMX95_MU_RX_FULL_BITS  0x0000000f
#define ELE_SUCCESS_IND 0x000000d6U

#endif /* HW_MISC_IMX95_MU_REGS_H */
