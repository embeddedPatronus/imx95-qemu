/*
 * NXP PCAL6408A I2C GPIO expander
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/gpio/pcal6408a.h"
#include "migration/vmstate.h"
#include "qemu/module.h"

enum {
    PCAL6408A_INPUT_PORT         = 0x00,
    PCAL6408A_OUTPUT_PORT        = 0x01,
    PCAL6408A_POLARITY_INVERSION = 0x02,
    PCAL6408A_CONFIGURATION      = 0x03,
    PCAL6408A_OUTPUT_DRIVE_0     = 0x40,
    PCAL6408A_OUTPUT_DRIVE_1     = 0x41,
    PCAL6408A_INPUT_LATCH        = 0x42,
    PCAL6408A_PULL_ENABLE        = 0x43,
    PCAL6408A_PULL_SELECTION     = 0x44,
    PCAL6408A_INTERRUPT_MASK     = 0x45,
    PCAL6408A_INTERRUPT_STATUS   = 0x46,
    PCAL6408A_OUTPUT_PORT_CONFIG = 0x4f,
};

static void pcal6408a_reset_regs(PCAL6408AState *s)
{
    memset(s->regs, 0, sizeof(s->regs));

    s->regs[PCAL6408A_INPUT_PORT] = 0xff;
    s->regs[PCAL6408A_OUTPUT_PORT] = 0x00;
    s->regs[PCAL6408A_POLARITY_INVERSION] = 0x00;
    s->regs[PCAL6408A_CONFIGURATION] = 0xff;
    s->regs[PCAL6408A_OUTPUT_DRIVE_0] = 0x00;
    s->regs[PCAL6408A_OUTPUT_DRIVE_1] = 0x00;
    s->regs[PCAL6408A_INPUT_LATCH] = 0x00;
    s->regs[PCAL6408A_PULL_ENABLE] = 0x00;
    s->regs[PCAL6408A_PULL_SELECTION] = 0x00;
    s->regs[PCAL6408A_INTERRUPT_MASK] = 0xff;
    s->regs[PCAL6408A_INTERRUPT_STATUS] = 0x00;
    s->regs[PCAL6408A_OUTPUT_PORT_CONFIG] = 0x00;

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
    } else {
        s->regs[s->reg_ptr++] = data;
    }

    return 0;
}

static uint8_t pcal6408a_recv(I2CSlave *i2c)
{
    PCAL6408AState *s = PCAL6408A(i2c);

    return s->regs[s->reg_ptr++];
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
        VMSTATE_UINT8_ARRAY(regs, PCAL6408AState,
                            PCAL6408A_REG_COUNT),
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
    .name = TYPE_PCAL6408A,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(PCAL6408AState),
    .class_init = pcal6408a_class_init,
};

static void pcal6408a_register_types(void)
{
    type_register_static(&pcal6408a_info);
}

type_init(pcal6408a_register_types)

