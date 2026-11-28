/*
 * NXP i.MX95 FlexSPI
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_IMX95_FSPI_H
#define HW_MISC_IMX95_FSPI_H

#include "hw/core/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qemu/units.h"

#define IMX95_FSPI_REG_SIZE     0x00010000
#define IMX95_FSPI_REG_COUNT    (IMX95_FSPI_REG_SIZE / sizeof(uint32_t))
#define IMX95_FSPI_AHB_SIZE     (128 * MiB)
#define IMX95_FSPI_NUM_CS       4       /* A1, A2, B1 and B2 */
#define IMX95_FSPI_NUM_SEQ      32      /* 128 LUT words, 4 per sequence */

/* The IP receive FIFO is drained by software, IPCR1[IDATSZ] has 16 bits */
#define IMX95_FSPI_RX_MAX       (0x10000 + 8)
#define IMX95_FSPI_TX_MAX       1024    /* 128 entries of 64 bits */
#define IMX95_FSPI_TX_WINDOW    128     /* TFDR0 to TFDR31 */
#define IMX95_FSPI_AHB_CHUNK    256 

#define TYPE_IMX95_FSPI "imx95.fspi"
OBJECT_DECLARE_SIMPLE_TYPE(Imx95FspiState, IMX95_FSPI)

struct Imx95FspiState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;         /* registers, FIFOs and LUT */
    MemoryRegion ahb_iomem;     /* AHB access memory map of the flash */

    SSIBus *spi;
    qemu_irq irq;
    qemu_irq cs[IMX95_FSPI_NUM_CS];

    uint32_t regs[IMX95_FSPI_REG_COUNT];

    uint32_t intr;              /* latched INTR flags */
    uint32_t sts0;
    uint32_t sts1;
    bool lut_key_armed;

    uint8_t rx_buf[IMX95_FSPI_RX_MAX];
    uint32_t rx_len;
    uint32_t rx_head;
    uint8_t tx_buf[IMX95_FSPI_TX_MAX];
    uint32_t tx_len;
    uint8_t tx_win[IMX95_FSPI_TX_WINDOW];

    uint8_t instr_ptr[IMX95_FSPI_NUM_CS];

    /* The AHB read buffer */
    uint8_t ahb_buf[IMX95_FSPI_AHB_CHUNK];
    bool ahb_valid;
    uint32_t ahb_base;
    uint32_t ahb_len;
};

#endif /* HW_MISC_IMX95_FSPI_H */
