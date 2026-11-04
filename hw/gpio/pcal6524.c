/*
 * NXP PCAL6524 24-bit I2C GPIO expander
 *
 * I2C slave model of the 24-bit GPIO expander: command and register access
 * with auto-increment, output pin lines and an interrupt line.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/irq.h"
#include "hw/gpio/pcal6524.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "qemu/module.h"

#define PCAL6524_CMD_AI                 0x80
#define PCAL6524_CMD_REG_MASK           0x7f

#define PCAL6524_INPUT_PORT0            0x00
#define PCAL6524_OUTPUT_PORT0           0x04
#define PCAL6524_POLARITY_PORT0         0x08
#define PCAL6524_CONFIG_PORT0           0x0c
#define PCAL6524_OUTPUT_DRIVE0          0x40
#define PCAL6524_INPUT_LATCH0           0x48
#define PCAL6524_PULL_ENABLE0           0x4c
#define PCAL6524_PULL_SELECT0           0x50
#define PCAL6524_INT_MASK0              0x54
#define PCAL6524_INT_STATUS0            0x58
#define PCAL6524_OUTPUT_CONFIG          0x5c
#define PCAL6524_INT_EDGE0              0x60
#define PCAL6524_INT_CLEAR0             0x68
#define PCAL6524_INPUT_STATUS0          0x6c
#define PCAL6524_PIN_OUTPUT_CONFIG0     0x70
#define PCAL6524_DEBOUNCE_ENABLE0       0x74
#define PCAL6524_DEBOUNCE_COUNT         0x76

static bool pcal6524_is_port_reg(uint8_t reg, uint8_t base, unsigned count)
{
    return reg >= base && reg < base + count;
}

static void pcal6524_update_irq(PCAL6524State *s)
{
    bool active = false;
    unsigned int port;

    for (port = 0; port < PCAL6524_NUM_PORTS; port++) {
        uint8_t status = s->regs[PCAL6524_INT_STATUS0 + port];
        uint8_t mask = s->regs[PCAL6524_INT_MASK0 + port];

        active |= !!(status & ~mask);
    }

    qemu_set_irq(s->irq, active);
}

static void pcal6524_update_outputs(PCAL6524State *s)
{
    unsigned int pin;

    for (pin = 0; pin < PCAL6524_NUM_PINS; pin++) {
        unsigned int port = pin / 8;
        uint8_t bit = 1U << (pin % 8);
        bool output = !(s->regs[PCAL6524_CONFIG_PORT0 + port] & bit);
        bool level = !!(s->regs[PCAL6524_OUTPUT_PORT0 + port] & bit);

        qemu_set_irq(s->gpio_out[pin], output && level);
    }
}

static uint8_t pcal6524_input_port(PCAL6524State *s, unsigned int port)
{
    uint8_t config = s->regs[PCAL6524_CONFIG_PORT0 + port];
    uint8_t output = s->regs[PCAL6524_OUTPUT_PORT0 + port];
    uint8_t input = (s->ext_state[port] & config) |
                    (output & ~config);

    return input ^ s->regs[PCAL6524_POLARITY_PORT0 + port];
}

static uint8_t pcal6524_read(PCAL6524State *s, uint8_t reg)
{
    if (pcal6524_is_port_reg(reg, PCAL6524_INPUT_PORT0,
                             PCAL6524_NUM_PORTS)) {
        return pcal6524_input_port(s, reg - PCAL6524_INPUT_PORT0);
    }

    if (pcal6524_is_port_reg(reg, PCAL6524_INPUT_STATUS0,
                             PCAL6524_NUM_PORTS)) {
        return pcal6524_input_port(s, reg - PCAL6524_INPUT_STATUS0);
    }

    if (reg < PCAL6524_NUM_REGS) {
        return s->regs[reg];
    }

    return 0xff;
}

static void pcal6524_write(PCAL6524State *s, uint8_t reg, uint8_t value)
{
    unsigned int port;

    if (reg >= PCAL6524_NUM_REGS) {
        return;
    }

    if (pcal6524_is_port_reg(reg, PCAL6524_INPUT_PORT0,
                             PCAL6524_NUM_PORTS) ||
        pcal6524_is_port_reg(reg, PCAL6524_INT_STATUS0,
                             PCAL6524_NUM_PORTS) ||
        pcal6524_is_port_reg(reg, PCAL6524_INPUT_STATUS0,
                             PCAL6524_NUM_PORTS)) {
        return;
    }

    if (pcal6524_is_port_reg(reg, PCAL6524_INT_CLEAR0,
                             PCAL6524_NUM_PORTS)) {
        port = reg - PCAL6524_INT_CLEAR0;
        s->regs[PCAL6524_INT_STATUS0 + port] &= ~value;
        pcal6524_update_irq(s);
        return;
    }

    s->regs[reg] = value;

    if (pcal6524_is_port_reg(reg, PCAL6524_OUTPUT_PORT0,
                             PCAL6524_NUM_PORTS) ||
        pcal6524_is_port_reg(reg, PCAL6524_CONFIG_PORT0,
                             PCAL6524_NUM_PORTS)) {
        pcal6524_update_outputs(s);
    }

    if (pcal6524_is_port_reg(reg, PCAL6524_INT_MASK0,
                             PCAL6524_NUM_PORTS)) {
        pcal6524_update_irq(s);
    }
}

static void pcal6524_advance_pointer(PCAL6524State *s)
{
    if (s->auto_increment) {
        s->reg_ptr = (s->reg_ptr + 1) & PCAL6524_CMD_REG_MASK;
    }
}

static int pcal6524_event(I2CSlave *i2c, enum i2c_event event)
{
    PCAL6524State *s = PCAL6524(i2c);

    qemu_log_mask(LOG_GUEST_ERROR,
                  "pcal6524: event=%u\n",
                  event);

    switch (event) {
    case I2C_START_SEND:
        s->have_reg_ptr = false;
        break;
    case I2C_FINISH:
        s->have_reg_ptr = false;
        break;
    default:
        break;
    }

    return 0;
}

static int pcal6524_send(I2CSlave *i2c, uint8_t data)
{
    PCAL6524State *s = PCAL6524(i2c);

    qemu_log_mask(LOG_GUEST_ERROR,
                  "pcal6524: send data=0x%02x have_ptr=%u\n",
                  data, s->have_reg_ptr);

    if (!s->have_reg_ptr) {
        s->auto_increment = data & PCAL6524_CMD_AI;
        s->reg_ptr = data & PCAL6524_CMD_REG_MASK;
        s->have_reg_ptr = true;
    } else {
        pcal6524_write(s, s->reg_ptr, data);
        pcal6524_advance_pointer(s);
    }

    return 0;
}

static uint8_t pcal6524_recv(I2CSlave *i2c)
{
    PCAL6524State *s = PCAL6524(i2c);
    uint8_t value = pcal6524_read(s, s->reg_ptr);

     qemu_log_mask(LOG_GUEST_ERROR,
                  "pcal6524: recv reg=0x%02x value=0x%02x\n",
                  s->reg_ptr, value);
    pcal6524_advance_pointer(s);
    return value;
}

static void pcal6524_gpio_input(void *opaque, int pin, int level)
{
    PCAL6524State *s = opaque;
    unsigned int port = pin / 8;
    uint8_t bit = 1U << (pin % 8);
    uint8_t old = s->ext_state[port];

    if (level) {
        s->ext_state[port] |= bit;
    } else {
        s->ext_state[port] &= ~bit;
    }

    if ((old ^ s->ext_state[port]) & bit) {
        s->regs[PCAL6524_INT_STATUS0 + port] |= bit;
        pcal6524_update_irq(s);
    }
}

static void pcal6524_reset(DeviceState *dev)
{
    PCAL6524State *s = PCAL6524(dev);
    unsigned int port;

    memset(s->regs, 0, sizeof(s->regs));
    memset(s->ext_state, 0xff, sizeof(s->ext_state));

    for (port = 0; port < PCAL6524_NUM_PORTS; port++) {
        s->regs[PCAL6524_OUTPUT_PORT0 + port] = 0xff;
        s->regs[PCAL6524_CONFIG_PORT0 + port] = 0xff;
        s->regs[PCAL6524_PULL_SELECT0 + port] = 0xff;
        s->regs[PCAL6524_INT_MASK0 + port] = 0xff;
    }

    s->regs[PCAL6524_OUTPUT_CONFIG] = 0x00;
    s->reg_ptr = 0;
    s->auto_increment = false;
    s->have_reg_ptr = false;

    pcal6524_update_outputs(s);
    pcal6524_update_irq(s);
}

static const VMStateDescription vmstate_pcal6524 = {
    .name = TYPE_PCAL6524,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(regs, PCAL6524State, PCAL6524_NUM_REGS),
        VMSTATE_UINT8_ARRAY(ext_state, PCAL6524State, PCAL6524_NUM_PORTS),
        VMSTATE_UINT8(reg_ptr, PCAL6524State),
        VMSTATE_BOOL(auto_increment, PCAL6524State),
        VMSTATE_BOOL(have_reg_ptr, PCAL6524State),
        VMSTATE_I2C_SLAVE(parent_obj, PCAL6524State),
        VMSTATE_END_OF_LIST()
    }
};

static void pcal6524_realize(DeviceState *dev, Error **errp)
{
    PCAL6524State *s = PCAL6524(dev);

    qdev_init_gpio_in(dev, pcal6524_gpio_input, PCAL6524_NUM_PINS);
    qdev_init_gpio_out(dev, s->gpio_out, PCAL6524_NUM_PINS);
    qdev_init_gpio_out_named(dev, &s->irq, "int", 1);
}

static void pcal6524_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *isc = I2C_SLAVE_CLASS(klass);

    dc->desc = "NXP PCAL6524 24-bit I2C GPIO expander";
    dc->realize = pcal6524_realize;
    dc->vmsd = &vmstate_pcal6524;
    device_class_set_legacy_reset(dc, pcal6524_reset);

    isc->event = pcal6524_event;
    isc->send = pcal6524_send;
    isc->recv = pcal6524_recv;
}

static const TypeInfo pcal6524_info = {
    .name = TYPE_PCAL6524,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(PCAL6524State),
    .class_init = pcal6524_class_init,
};

static void pcal6524_register_types(void)
{
    type_register_static(&pcal6524_info);
}

type_init(pcal6524_register_types)
