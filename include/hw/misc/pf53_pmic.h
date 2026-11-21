/*
 * NXP PF53 PMIC
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_PF53_PMIC_H
#define HW_MISC_PF53_PMIC_H

#include "hw/i2c/i2c.h"

#define TYPE_PF53_PMIC "pf53-pmic"
OBJECT_DECLARE_SIMPLE_TYPE(PF53PMICState, PF53_PMIC)

#define PF53_PMIC_REG_COUNT 256

struct PF53PMICState {
    I2CSlave parent_obj;

    uint8_t regs[PF53_PMIC_REG_COUNT];
    uint8_t reg_ptr;
    bool have_reg_ptr;
};

#endif /* HW_MISC_PF53_PMIC_H */

