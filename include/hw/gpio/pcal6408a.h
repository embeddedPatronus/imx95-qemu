/*
 * NXP PCAL6408A I2C GPIO expander
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_GPIO_PCAL6408A_H
#define HW_GPIO_PCAL6408A_H

#include "hw/i2c/i2c.h"

#define TYPE_PCAL6408A "pcal6408a"
OBJECT_DECLARE_SIMPLE_TYPE(PCAL6408AState, PCAL6408A)

#define PCAL6408A_REG_COUNT 256

struct PCAL6408AState {
    I2CSlave parent_obj;

    uint8_t regs[PCAL6408A_REG_COUNT];
    uint8_t reg_ptr;
    bool have_reg_ptr;
};

#endif /* HW_GPIO_PCAL6408A_H */

