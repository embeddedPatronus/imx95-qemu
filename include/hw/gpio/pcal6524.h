/*
 * NXP PCAL6524 24-bit I2C GPIO expander
 *
 * State and type definitions of the model in hw/gpio/pcal6524.c.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_GPIO_PCAL6524_H
#define HW_GPIO_PCAL6524_H

#include "hw/i2c/i2c.h"
#include "qom/object.h"

#define TYPE_PCAL6524 "pcal6524"
OBJECT_DECLARE_SIMPLE_TYPE(PCAL6524State, PCAL6524)

#define PCAL6524_NUM_PORTS 3
#define PCAL6524_NUM_PINS  24
#define PCAL6524_NUM_REGS  0x77

struct PCAL6524State {
    I2CSlave parent_obj;

    uint8_t regs[PCAL6524_NUM_REGS];
    uint8_t ext_state[PCAL6524_NUM_PORTS];
    uint8_t reg_ptr;
    bool auto_increment;
    bool have_reg_ptr;

    qemu_irq gpio_out[PCAL6524_NUM_PINS];
    qemu_irq irq;
};

#endif /* HW_GPIO_PCAL6524_H */
