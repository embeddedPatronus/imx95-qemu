/*
 * NXP PF09 PMIC
 *
 * Minimal I2C register-map model.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_PF09_PMIC_H
#define HW_MISC_PF09_PMIC_H

#include "hw/i2c/i2c.h"
#include "hw/i2c/imx_lpi2c.h"
#include "qom/object.h"

#define TYPE_PF09_PMIC "pf09-pmic"
OBJECT_DECLARE_SIMPLE_TYPE(PF09PMICState, PF09_PMIC)

typedef struct PF09PMICState {
    I2CSlave parent_obj;

    uint8_t regs[256];
    uint8_t reg_ptr;
    bool have_reg_ptr;

    uint8_t read_reg;
    uint8_t read_value;
    uint8_t read_pos;

    uint8_t write_reg;
    uint8_t write_value;
    uint8_t write_pos;
} PF09PMICState;

#endif /* HW_MISC_PF09_PMIC_H */
