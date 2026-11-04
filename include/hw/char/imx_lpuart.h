/*
 * i.MX LPUART(Low Power UART)
 *
 * State and type definitions of the model in hw/char/imx_lpuart.c.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 * Author: Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef IMX_LPUART_H
#define IMX_LPUART_H

#include "hw/core/sysbus.h"
#include "chardev/char-fe.h"
#include "qom/object.h"

#define TYPE_IMX_LPUART "imx.lpuart"
OBJECT_DECLARE_SIMPLE_TYPE(IMXLPUARTState, IMX_LPUART)

#define IMX_LPUART_MMIO_SIZE 0x10000
#define IMX_LPUART_RX_FIFO_SIZE 32

#define LPUART_VERID        0x00
#define LPUART_PARAM        0x04
#define LPUART_GLOBAL       0x08
#define LPUART_PINCFG       0x0C
#define LPUART_BAUD         0x10
#define LPUART_STAT         0x14
#define LPUART_CTRL         0x18
#define LPUART_DATA         0x1C
#define LPUART_MATCH        0x20
#define LPUART_MODIR        0x24
#define LPUART_FIFO         0x28
#define LPUART_WATER        0x2C

#define LPUART_STAT_PF      BIT(16)
#define LPUART_STAT_FE      BIT(17)
#define LPUART_STAT_NF      BIT(18)
#define LPUART_STAT_OR      BIT(19)
#define LPUART_STAT_IDLE    BIT(20)
#define LPUART_STAT_RDRF    BIT(21)
#define LPUART_STAT_TC      BIT(22)
#define LPUART_STAT_TDRE    BIT(23)

#define LPUART_CTRL_RE      BIT(18)
#define LPUART_CTRL_TE      BIT(19)
#define LPUART_CTRL_RIE     BIT(21)
#define LPUART_CTRL_TCIE    BIT(22)
#define LPUART_CTRL_TIE     BIT(23)

#define LPUART_WATER_TXCOUNT_MASK   (0x1F00U)
#define LPUART_WATER_TXCOUNT_SHIFT  (8U)

#define LPUART_WATER_RXCOUNT_MASK   (0x1F000000U)
#define LPUART_WATER_RXCOUNT_SHIFT  (24U)

#define LPUART_WATER_TXCOUNT(x) \
    (((uint32_t)(x) << LPUART_WATER_TXCOUNT_SHIFT) & LPUART_WATER_TXCOUNT_MASK)

#define LPUART_WATER_RXCOUNT(x) \
    (((uint32_t)(x) << LPUART_WATER_RXCOUNT_SHIFT) & LPUART_WATER_RXCOUNT_MASK)

#define LPUART_FIFO_RXEMPT_MASK   (0x400000U)
#define LPUART_FIFO_RXEMPT_SHIFT  (22U)

#define LPUART_FIFO_TXEMPT_MASK   (0x800000U)
#define LPUART_FIFO_TXEMPT_SHIFT  (23U)

#define LPUART_FIFO_RXFE_MASK     (0x8U)
#define LPUART_FIFO_RXFE_SHIFT    (3U)

#define LPUART_FIFO_TXFE_MASK     (0x80U)
#define LPUART_FIFO_TXFE_SHIFT    (7U)

#define LPUART_FIFO_RXFLUSH_MASK  (0x4000U)
#define LPUART_FIFO_TXFLUSH_MASK  (0x8000U)

#define LPUART_FIFO_RXUF_MASK     (0x10000U)
#define LPUART_FIFO_TXOF_MASK     (0x20000U)


struct IMXLPUARTState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    uint32_t verid;
    uint32_t param;
    uint32_t global;
    uint32_t pincfg;
    uint32_t baud;
    uint32_t stat;
    uint32_t ctrl;
    uint32_t data;
    uint32_t match;
    uint32_t modir;
    uint32_t fifo;
    uint32_t water;

    uint8_t rx_fifo[IMX_LPUART_RX_FIFO_SIZE];
    uint32_t rx_head;
    uint32_t rx_tail;
    uint32_t rx_count;

    qemu_irq irq;
    CharFrontend chr;
};

#endif
