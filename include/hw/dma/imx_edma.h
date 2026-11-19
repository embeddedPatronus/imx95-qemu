/*
 * NXP i.MX enhanced Direct Memory Access controller 
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_DMA_IMX_EDMA_H
#define HW_DMA_IMX_EDMA_H

#include "hw/core/sysbus.h"
#include "system/memory.h"
#include "qom/object.h"

#define TYPE_IMX_EDMA "imx.edma"
OBJECT_DECLARE_SIMPLE_TYPE(IMXEDMAState, IMX_EDMA)

#define IMX_EDMA_V3             3
#define IMX_EDMA_V5             5

#define IMX_EDMA_MAX_CHANNELS   64
#define IMX_EDMA_MAX_REQUESTS   128
#define IMX_EDMA_TCD_BYTES      64

struct IMXEDMATcdLayout;

struct IMXEDMAState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    MemoryRegion *dma_mr;
    AddressSpace dma_as;
    QEMUBH *bh;

    qemu_irq ch_irq[IMX_EDMA_MAX_CHANNELS];
    qemu_irq err_irq;

    /* Properties */
    uint32_t version;        
    uint32_t num_channels;
    uint32_t num_requests;
    bool paired_irqs;     

    const struct IMXEDMATcdLayout *layout;
    uint32_t ch_base;
    uint32_t ch_stride;
    uint32_t csr_rw_mask;
    uint32_t mp_csr_rw_mask;

    /* Management page */
    uint32_t mp_csr;
    uint32_t mp_es;
    uint32_t grpri[IMX_EDMA_MAX_CHANNELS];
    uint32_t mux[IMX_EDMA_MAX_CHANNELS];
    uint32_t prot[IMX_EDMA_MAX_CHANNELS];
    uint32_t rr_next;

    uint32_t req[IMX_EDMA_MAX_REQUESTS / 32];

    /* Channels */
    uint32_t ch_csr[IMX_EDMA_MAX_CHANNELS];
    uint32_t ch_es[IMX_EDMA_MAX_CHANNELS];
    uint32_t ch_int[IMX_EDMA_MAX_CHANNELS];
    uint32_t ch_sbr[IMX_EDMA_MAX_CHANNELS];
    uint32_t ch_pri[IMX_EDMA_MAX_CHANNELS];
    uint32_t ch_mattr[IMX_EDMA_MAX_CHANNELS];
    uint8_t tcd[IMX_EDMA_MAX_CHANNELS * IMX_EDMA_TCD_BYTES];
};

#endif /* HW_DMA_IMX_EDMA_H */
