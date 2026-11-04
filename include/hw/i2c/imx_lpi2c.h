/*
 * NXP i.MX LPI2C controller
 *
 * State and type definitions of the model in hw/i2c/imx_lpi2c.c.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_I2C_IMX_LPI2C_H
#define HW_I2C_IMX_LPI2C_H

#include "hw/core/sysbus.h"
#include "hw/i2c/i2c.h"
#include "qom/object.h"

#define IMX_LPI2C_MMIO_SIZE 0x10000

#define TYPE_IMX_LPI2C "imx.lpi2c"
OBJECT_DECLARE_SIMPLE_TYPE(IMXLPI2CState, IMX_LPI2C)

typedef struct IMXLPI2CState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    I2CBus *bus;

    uint32_t regs[IMX_LPI2C_MMIO_SIZE / 4];

    uint8_t rx_fifo[16];
    unsigned rx_len;
    unsigned rx_pos;
} IMXLPI2CState;

#endif /* HW_I2C_IMX_LPI2C_H */
