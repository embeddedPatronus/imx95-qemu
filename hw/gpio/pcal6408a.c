/*
 * NXP PCAL6408A I2C GPIO expander
 *
 * Minimal register-backed model for board bring-up.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/gpio/pcal6408a.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

static void pcal6408a_reset_regs(PCAL6408AState *s)
{
    memset(s->regs, 0, sizeof(s->regs));

    /*
     *   reset/defaults:
     *   input pins high
     *   output pins low
     *   polarity normal
     *   all pins input
     *   interrupt mask all masked
     */
    s->regs[BUS_EXP_IN_REG] = 0xff;
    s->regs[BUS_EXP_OUT_REG] = 0x00;
    s->regs[BUS_EXP_POL_REG] = 0x00;
    s->regs[BUS_EXP_CONF_REG] = 0xff;
    s->regs[BUS_EXP_DS0_REG] = 0x00;
    s->regs[BUS_EXP_DS1_REG] = 0x00;
    s->regs[BUS_EXP_LATCH_REG] = 0x00;
    s->regs[BUS_EXP_PULL1_REG] = 0x00;
    s->regs[BUS_EXP_PULL2_REG] = 0x00;
    s->regs[BUS_EXP_MASK_REG] = 0xff;
    s->regs[BUS_EXP_STAT_REG] = 0x00;
    s->regs[BUS_EXP_OD_REG] = 0x00;

    s->reg_ptr = 0;
    s->have_reg_ptr = false;
}

static int pcal6408a_event(I2CSlave *i2c, enum i2c_event event)
{
    PCAL6408AState *s = PCAL6408A(i2c);

    switch (event) {
    case I2C_START_SEND:
        s->have_reg_ptr = false;
        break;

    case I2C_START_RECV:
        break;

    case I2C_FINISH:
        s->have_reg_ptr = false;
        break;

    default:
        break;
    }

    return 0;
}

static int pcal6408a_send(I2CSlave *i2c, uint8_t data)
{
    PCAL6408AState *s = PCAL6408A(i2c);

    if (!s->have_reg_ptr) {
        s->reg_ptr = data;
        s->have_reg_ptr = true;

        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: set reg_ptr=0x%02x\n",
                      TYPE_PCAL6408A, s->reg_ptr);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write reg[0x%02x] = 0x%02x\n",
                      TYPE_PCAL6408A, s->reg_ptr, data);

        s->regs[s->reg_ptr++] = data;
    }

    return 0;
}

static uint8_t pcal6408a_recv(I2CSlave *i2c)
{
    PCAL6408AState *s = PCAL6408A(i2c);
    uint8_t reg = s->reg_ptr;
    uint8_t value = s->regs[s->reg_ptr++];

    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: read reg[0x%02x] -> 0x%02x\n",
                  TYPE_PCAL6408A, reg, value);

    return value;
}

static void pcal6408a_reset(DeviceState *dev)
{
    PCAL6408AState *s = PCAL6408A(dev);

    pcal6408a_reset_regs(s);
}

static const VMStateDescription vmstate_pcal6408a = {
    .name = TYPE_PCAL6408A,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(regs, PCAL6408AState, 256),
        VMSTATE_UINT8(reg_ptr, PCAL6408AState),
        VMSTATE_BOOL(have_reg_ptr, PCAL6408AState),
        VMSTATE_END_OF_LIST()
    },
};

static void pcal6408a_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *isc = I2C_SLAVE_CLASS(klass);

    dc->vmsd = &vmstate_pcal6408a;
    device_class_set_legacy_reset(dc, pcal6408a_reset);
    dc->desc = "NXP PCAL6408A I2C GPIO expander";

    isc->event = pcal6408a_event;
    isc->send = pcal6408a_send;
    isc->recv = pcal6408a_recv;
}

static const TypeInfo pcal6408a_info = {
    .name          = TYPE_PCAL6408A,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(PCAL6408AState),
    .class_init    = pcal6408a_class_init,
};

static void pcal6408a_register_types(void)
{
    type_register_static(&pcal6408a_info);
}

type_init(pcal6408a_register_types)
