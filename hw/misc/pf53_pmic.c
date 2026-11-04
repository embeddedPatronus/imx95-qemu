/*
 * NXP PF53 PMIC
 *
 * Minimal I2C register-map model.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/pf53_pmic.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define PF53_REG_DEV_ID    0x00
#define PF53_REG_REV_ID    0x01
#define PF53_REG_EMREV_ID  0x02
#define PF53_REG_PROG_ID   0x03

static void pf53_reset_regs(PF53PMICState *s)
{
    memset(s->regs, 0, sizeof(s->regs));

    s->regs[PF53_REG_DEV_ID] = 0x53;
    s->regs[PF53_REG_REV_ID] = 0x01;
    s->regs[PF53_REG_EMREV_ID] = 0x00;
    s->regs[PF53_REG_PROG_ID] = 0x00;

    s->reg_ptr = 0;
    s->have_reg_ptr = false;
}

static int pf53_event(I2CSlave *i2c, enum i2c_event event)
{
    PF53PMICState *s = PF53_PMIC(i2c);

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

static int pf53_send(I2CSlave *i2c, uint8_t data)
{
    PF53PMICState *s = PF53_PMIC(i2c);

    if (!s->have_reg_ptr) {
        s->reg_ptr = data;
        s->have_reg_ptr = true;

        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: set reg_ptr=0x%02x\n",
                      TYPE_PF53_PMIC, s->reg_ptr);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write reg[0x%02x] = 0x%02x\n",
                      TYPE_PF53_PMIC, s->reg_ptr, data);

        s->regs[s->reg_ptr++] = data;
    }

    return 0;
}

static uint8_t pf53_recv(I2CSlave *i2c)
{
    PF53PMICState *s = PF53_PMIC(i2c);
    uint8_t reg = s->reg_ptr;
    uint8_t value = s->regs[s->reg_ptr++];

    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: read reg[0x%02x] -> 0x%02x\n",
                  TYPE_PF53_PMIC, reg, value);

    return value;
}

static void pf53_pmic_reset(DeviceState *dev)
{
    PF53PMICState *s = PF53_PMIC(dev);

    pf53_reset_regs(s);
}

static const VMStateDescription vmstate_pf53_pmic = {
    .name = TYPE_PF53_PMIC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(regs, PF53PMICState, 256),
        VMSTATE_UINT8(reg_ptr, PF53PMICState),
        VMSTATE_BOOL(have_reg_ptr, PF53PMICState),
        VMSTATE_END_OF_LIST()
    },
};

static void pf53_pmic_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *isc = I2C_SLAVE_CLASS(klass);

    dc->vmsd = &vmstate_pf53_pmic;
    device_class_set_legacy_reset(dc, pf53_pmic_reset);
    dc->desc = "NXP PF53 PMIC";

    isc->event = pf53_event;
    isc->send = pf53_send;
    isc->recv = pf53_recv;
}

static const TypeInfo pf53_pmic_info = {
    .name          = TYPE_PF53_PMIC,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(PF53PMICState),
    .class_init    = pf53_pmic_class_init,
};

static void pf53_pmic_register_types(void)
{
    type_register_static(&pf53_pmic_info);
}

type_init(pf53_pmic_register_types)
