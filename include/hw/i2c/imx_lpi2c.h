/*
 * NXP i.MX LPI2C controller
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_I2C_IMX_LPI2C_H
#define HW_I2C_IMX_LPI2C_H

#include "hw/core/sysbus.h"
#include "hw/i2c/i2c.h"

#define IMX_LPI2C_MMIO_SIZE    0x10000
#define IMX_LPI2C_RX_FIFO_SIZE 16

#define TYPE_IMX_LPI2C "imx.lpi2c"
OBJECT_DECLARE_SIMPLE_TYPE(IMXLPI2CState, IMX_LPI2C)

typedef struct IMXLPI2CState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    qemu_irq tx_dma_req;
    qemu_irq rx_dma_req;
    bool tx_req;
    bool rx_req;
    I2CBus *bus;

    uint32_t regs[IMX_LPI2C_MMIO_SIZE / 4];

    uint8_t rx_fifo[IMX_LPI2C_RX_FIFO_SIZE];
    uint32_t rx_len;
    uint32_t rx_pos;
    uint32_t rx_pending;
    bool stop_pending;
} IMXLPI2CState;

#endif /* HW_I2C_IMX_LPI2C_H */

