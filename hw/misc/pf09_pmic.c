/*
 * NXP PF09 PMIC model
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/pf09_pmic.h"
#include "migration/vmstate.h"
#include "qemu/module.h"

#define PF09_DEVICE_ID_REG       0x00
#define PF09_REVISION_ID_REG     0x01
#define PF09_EMREV_REG           0x02

#define PF09_DEVICE_ID_RESET     0x09
#define PF09_REVISION_ID_RESET   0x23
#define PF09_EMREV_RESET         0x01

#define PF09_CRC_INITIAL_VALUE   0xff
#define PF09_CRC_POLYNOMIAL      0x1d

static uint8_t pf09_crc_j1850(const uint8_t *buf, unsigned int len)
{
    uint8_t crc = PF09_CRC_INITIAL_VALUE;
    unsigned int byte;

    for (byte = 0; byte < len; byte++) {
        unsigned int bit;

        crc ^= buf[byte];
        for (bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ PF09_CRC_POLYNOMIAL;
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}

static uint8_t pf09_read_crc(PF09PMICState *s, I2CSlave *i2c)
{
    uint8_t crc_buf[3];

    crc_buf[0] = (i2c->address << 1) | 1;
    crc_buf[1] = s->read_reg;
    crc_buf[2] = s->read_value;

    return pf09_crc_j1850(crc_buf, ARRAY_SIZE(crc_buf));
}

static void pf09_reset_regs(PF09PMICState *s)
{
    memset(s->regs, 0, sizeof(s->regs));

    s->regs[PF09_DEVICE_ID_REG] = PF09_DEVICE_ID_RESET;
    s->regs[PF09_REVISION_ID_REG] = PF09_REVISION_ID_RESET;
    s->regs[PF09_EMREV_REG] = PF09_EMREV_RESET;

    s->reg_ptr = 0;
    s->have_reg_ptr = false;

    s->read_reg = 0;
    s->read_value = 0;
    s->read_pos = 0;

    s->write_reg = 0;
    s->write_value = 0;
    s->write_pos = 0;
}

static int pf09_event(I2CSlave *i2c, enum i2c_event event)
{
    PF09PMICState *s = PF09_PMIC(i2c);

    switch (event) {
    case I2C_START_SEND:
        s->have_reg_ptr = false;
        s->write_pos = 0;
        break;
    case I2C_START_RECV:
        s->read_reg = s->reg_ptr;
        s->read_value = s->regs[s->read_reg];
        s->read_pos = 0;
        break;
    case I2C_FINISH:
        s->have_reg_ptr = false;
        s->read_pos = 0;
        s->write_pos = 0;
        break;
    default:
        break;
    }

    return 0;
}

static int pf09_send(I2CSlave *i2c, uint8_t data)
{
    PF09PMICState *s = PF09_PMIC(i2c);

    if (!s->have_reg_ptr) {
        s->reg_ptr = data;
        s->have_reg_ptr = true;
        s->write_reg = data;
        s->write_pos = 0;
        return 0;
    }

    if (s->write_pos == 0) {
        s->write_value = data;
        s->regs[s->write_reg] = data;
        s->write_pos = 1;
    } else {
        /* The second byte is the write CRC. It is accepted but not checked. */
        s->write_reg++;
        s->reg_ptr = s->write_reg;
        s->write_pos = 0;
    }

    return 0;
}

static uint8_t pf09_recv(I2CSlave *i2c)
{
    PF09PMICState *s = PF09_PMIC(i2c);
    uint8_t value;

    if (s->read_pos == 0) {
        s->read_reg = s->reg_ptr;
        s->read_value = s->regs[s->read_reg];
        value = s->read_value;
        s->read_pos = 1;
    } else {
        value = pf09_read_crc(s, i2c);
        s->read_pos = 0;
        s->reg_ptr = s->read_reg + 1;
    }

    return value;
}

static void pf09_pmic_reset(DeviceState *dev)
{
    PF09PMICState *s = PF09_PMIC(dev);

    pf09_reset_regs(s);
}

static int pf09_pmic_post_load(void *opaque, int version_id)
{
    PF09PMICState *s = opaque;

    if (s->read_pos > 1 || s->write_pos > 1) {
        return -EINVAL;
    }

    return 0;
}

static const VMStateDescription vmstate_pf09_pmic = {
    .name = TYPE_PF09_PMIC,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = pf09_pmic_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(regs, PF09PMICState, PF09_PMIC_REG_COUNT),
        VMSTATE_UINT8(reg_ptr, PF09PMICState),
        VMSTATE_BOOL(have_reg_ptr, PF09PMICState),
        VMSTATE_UINT8(read_reg, PF09PMICState),
        VMSTATE_UINT8(read_value, PF09PMICState),
        VMSTATE_UINT8(read_pos, PF09PMICState),
        VMSTATE_UINT8(write_reg, PF09PMICState),
        VMSTATE_UINT8(write_value, PF09PMICState),
        VMSTATE_UINT8(write_pos, PF09PMICState),
        VMSTATE_END_OF_LIST()
    },
};

static void pf09_pmic_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *isc = I2C_SLAVE_CLASS(klass);

    dc->vmsd = &vmstate_pf09_pmic;
    device_class_set_legacy_reset(dc, pf09_pmic_reset);
    dc->desc = "NXP PF09 PMIC";

    isc->event = pf09_event;
    isc->send = pf09_send;
    isc->recv = pf09_recv;
}

static const TypeInfo pf09_pmic_info = {
    .name = TYPE_PF09_PMIC,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(PF09PMICState),
    .class_init = pf09_pmic_class_init,
};

static void pf09_pmic_register_types(void)
{
    type_register_static(&pf09_pmic_info);
}

type_init(pf09_pmic_register_types)

