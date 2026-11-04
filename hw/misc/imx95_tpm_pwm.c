/*
 * NXP i.MX95 tpm_pwm
 *
 * Timer/PWM module (TPM) register model with eight channels. PARAM reports
 * the channel count and the counter registers start disabled with a 16-bit
 * modulo.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "hw/core/qdev-properties.h"
#include "hw/misc/imx95_tpm_pwm.h"
#include "migration/vmstate.h"
#include "hw/core/irq.h"
#include "system/memory.h"

#define IMX95_TPM_VERID                 0x000
#define IMX95_TPM_PARAM                 0x004
#define IMX95_TPM_GLOBAL                0x008
#define IMX95_TPM_SC                    0x010
#define IMX95_TPM_CNT                   0x014
#define IMX95_TPM_MOD                   0x018
#define IMX95_TPM_CNSC_BASE             0x020
#define IMX95_TPM_CNV_BASE              0x024
#define IMX95_TPM_CHANNEL_STRIDE        0x008

#define IMX95_TPM_CHANNEL_COUNT         8U
#define IMX95_TPM_PARAM_CHANNEL_MASK    MAKE_64BIT_MASK(0, 8)
#define IMX95_TPM_CNSC_CHF              BIT(7)


static bool imx95_tpm_pwm_channel_reg(hwaddr offset,
                                     unsigned int *channel,
                                     bool *is_control)
{
    hwaddr relative;
    hwaddr channel_offset;

    if (offset < IMX95_TPM_CNSC_BASE) {
        return false;
    }

    relative = offset - IMX95_TPM_CNSC_BASE;
    *channel = relative / IMX95_TPM_CHANNEL_STRIDE;
    channel_offset = relative % IMX95_TPM_CHANNEL_STRIDE;

    if (*channel >= IMX95_TPM_CHANNEL_COUNT) {
        return false;
    }

    if (channel_offset == 0) {
        *is_control = true;
        return true;
    }

    if (channel_offset == sizeof(uint32_t)) {
        *is_control = false;
        return true;
    }

    return false;
}

static uint64_t imx95_tpm_pwm_read(void *opaque,
                                  hwaddr offset,
                                  unsigned size)
{
    Imx95TpmPwmState *s = opaque;
    uint32_t value;

    if (size != sizeof(uint32_t) ||
        (offset & 3U) != 0U ||
        offset >= IMX95_TPM_PWM_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.tpm-pwm: invalid read "
                      "offset=0x%04" HWADDR_PRIx " size=%u\n",
                      offset, size);
        return 0;
    }

    value = s->regs[offset / sizeof(uint32_t)];

    if (offset == IMX95_TPM_PARAM ||
        offset == IMX95_TPM_GLOBAL ||
        offset == IMX95_TPM_SC ||
        offset == IMX95_TPM_CNT ||
        offset == IMX95_TPM_MOD) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.tpm-pwm: read "
                      "offset=0x%04" HWADDR_PRIx
                      " value=0x%08x\n",
                      offset, value);
    }

    return value;
}

static void imx95_tpm_pwm_write(void *opaque,
                                hwaddr offset,
                                uint64_t value,
                                unsigned size)
{
    Imx95TpmPwmState *s = opaque;
    uint32_t old_value;
    uint32_t new_value = value;
    unsigned int channel;
    bool is_control;

    if (size != sizeof(uint32_t) ||
        (offset & 3U) != 0U ||
        offset >= IMX95_TPM_PWM_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.tpm-pwm: invalid write "
                      "offset=0x%04" HWADDR_PRIx
                      " size=%u value=0x%08x\n",
                      offset, size, new_value);
        return;
    }

    old_value = s->regs[offset / sizeof(uint32_t)];

    switch (offset) {
    case IMX95_TPM_VERID:
    case IMX95_TPM_PARAM:
        /*
         * Hardware identification and capability registers are read-only.
         */
        return;
    default:
        break;
    }

    if (imx95_tpm_pwm_channel_reg(offset, &channel, &is_control) &&
        is_control) {
        /*
         * CnSC.CHF is write-one-to-clear. All remaining CnSC bits are
         * ordinary software-controlled configuration bits.
         */
        new_value = (new_value & ~IMX95_TPM_CNSC_CHF) |
                    ((old_value & IMX95_TPM_CNSC_CHF) &
                     ~(uint32_t)value);
    }

    s->regs[offset / sizeof(uint32_t)] = new_value;

    if (offset == IMX95_TPM_GLOBAL ||
        offset == IMX95_TPM_SC ||
        offset == IMX95_TPM_CNT ||
        offset == IMX95_TPM_MOD ||
        imx95_tpm_pwm_channel_reg(offset, &channel, &is_control)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.tpm-pwm: write "
                      "offset=0x%04" HWADDR_PRIx
                      " old=0x%08x value=0x%08x new=0x%08x\n",
                      offset, old_value, (uint32_t)value, new_value);
    }
}

static const MemoryRegionOps imx95_tpm_pwm_ops = {
    .read = imx95_tpm_pwm_read,
    .write = imx95_tpm_pwm_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void imx95_tpm_pwm_reset(DeviceState *dev)
{
    Imx95TpmPwmState *s = IMX95_TPM_PWM(dev);
    memset(s->regs, 0, sizeof(s->regs));


    s->regs[IMX95_TPM_PARAM / sizeof(uint32_t)] =
        IMX95_TPM_CHANNEL_COUNT & IMX95_TPM_PARAM_CHANNEL_MASK;

    s->regs[IMX95_TPM_SC / sizeof(uint32_t)] = 0;
    s->regs[IMX95_TPM_CNT / sizeof(uint32_t)] = 0;
    s->regs[IMX95_TPM_MOD / sizeof(uint32_t)] = UINT16_MAX;
}

static void imx95_tpm_pwm_init(Object *obj)
{
    Imx95TpmPwmState *s = IMX95_TPM_PWM(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_tpm_pwm_ops, s,
                          TYPE_IMX95_TPM_PWM, IMX95_TPM_PWM_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription vmstate_imx95_tpm_pwm = {
    .name = TYPE_IMX95_TPM_PWM,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, Imx95TpmPwmState, IMX95_TPM_PWM_SIZE / sizeof(uint32_t)),
        VMSTATE_END_OF_LIST()
    }
};

static void imx95_tpm_pwm_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, imx95_tpm_pwm_reset);
    dc->vmsd = &vmstate_imx95_tpm_pwm;
    dc->desc = "i.MX95 tpm pwm";
}

static const TypeInfo imx95_tpm_pwm_types[] = {
    {
        .name = TYPE_IMX95_TPM_PWM,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Imx95TpmPwmState),
        .instance_init = imx95_tpm_pwm_init,
        .class_init = imx95_tpm_pwm_class_init,
    },
};

DEFINE_TYPES(imx95_tpm_pwm_types)
