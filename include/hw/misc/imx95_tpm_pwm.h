/*
 * NXP i.MX95 tpm-pwm
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_IMX95_TPM_PWM_H
#define HW_MISC_IMX95_TPM_PWM_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define IMX95_TPM_PWM_SIZE              0x00010000


#define TYPE_IMX95_TPM_PWM "imx95.tpm-pwm"
OBJECT_DECLARE_SIMPLE_TYPE(Imx95TpmPwmState, IMX95_TPM_PWM)

struct Imx95TpmPwmState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t regs[IMX95_TPM_PWM_SIZE / sizeof(uint32_t)];
};

#endif /* HW_MISC_IMX95_TPM_PWM_H */
