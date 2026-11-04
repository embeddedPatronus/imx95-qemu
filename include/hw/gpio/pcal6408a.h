/*
 * NXP PCAL6408A I2C GPIO expander
 *
 * State and type definitions of the model in hw/gpio/pcal6408a.c.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_GPIO_PCAL6408A_H
#define HW_GPIO_PCAL6408A_H

#include "hw/i2c/i2c.h"
#include "qom/object.h"

#define BUS_EXP_IN_REG 0x00
#define BUS_EXP_OUT_REG 0x01
#define BUS_EXP_POL_REG 0x02
#define BUS_EXP_CONF_REG 0x03
#define BUS_EXP_DS0_REG 0x40
#define BUS_EXP_DS1_REG 0x41
#define BUS_EXP_LATCH_REG 0x42
#define BUS_EXP_PULL1_REG 0x43
#define BUS_EXP_PULL2_REG 0x44
#define BUS_EXP_MASK_REG 0x45
#define BUS_EXP_STAT_REG 0x46
#define BUS_EXP_OD_REG 0x4F


#define TYPE_PCAL6408A "pcal6408a"
OBJECT_DECLARE_SIMPLE_TYPE(PCAL6408AState, PCAL6408A)

typedef struct PCAL6408AState {
    I2CSlave parent_obj;

    uint8_t regs[256];
    uint8_t reg_ptr;
    bool have_reg_ptr;
} PCAL6408AState;

#endif /* HW_GPIO_PCAL6408A_H */
