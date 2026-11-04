/*
 * NXP i.MX95 sc mu
 *
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_IMX95_SC_MU_H
#define HW_MISC_IMX95_SC_MU_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define IMX95_SCMI_CLOCK_ID_MAX                0x100
#define IMX95_SCMI_SMT_SIZE        0x400

/* iMX8 MU register offsets */
#define IMX8_MU_TR0   0x00
#define IMX8_MU_RR0   0x10
#define IMX8_MU_SR    0x20
#define IMX8_MU_CR    0x24
/* All 4 TX-empty bits and all 4 RX-full bits */
#define IMX8_MU_SR_TE_ALL  (0xfu << 20)  /* TE3|TE2|TE1|TE0 */
#define IMX8_MU_SR_RF_ALL  (0xfu << 24)  /* RF3|RF2|RF1|RF0 */

/* SC RPC constants */
#define SC_RPC_VERSION      1u
#define SC_RPC_SVC_RETURN   1u
#define SC_RPC_MAX_MSG      8u
#define IMX95_SC_MU_SIZE        0x10000u

#define TYPE_IMX95_SC_MU "imx95.sc-mu"
OBJECT_DECLARE_SIMPLE_TYPE(Imx95ScMuState, IMX95_SC_MU)

struct Imx95ScMuState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t tr[4];          /* transmit shadow registers          */
    uint32_t rr[4];          /* receive (response) registers       */
    unsigned tx_count;       /* words written so far               */
    unsigned tx_size;        /* total words expected (from hdr)    */
    bool     rx_ready;       /* response is waiting to be read     */
    unsigned rx_count;       /* words read so far                  */
    unsigned rx_size;        /* total words in response            */
};

#endif /* HW_MISC_IMX95_SC_MU_H */
