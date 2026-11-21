/*
 * NXP PF53 PMIC model
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/pf53_pmic.h"
#include "migration/vmstate.h"
#include "qemu/module.h"

#define PF53_DEVICE_ID_REG       0x00
#define PF53_REVISION_ID_REG     0x01
#define PF53_EMREV_REG           0x02
#define PF53_PROGRAM_ID_REG      0x03

#define PF53_DEVICE_ID_RESET     0x53
#define PF53_REVISION_ID_RESET   0x01
#define PF53_EMREV_RESET         0x00
#define PF53_PROGRAM_ID_RESET    0x00

static void pf53_reset_regs(PF53PMICState *s)
{
    memset(s->regs, 0, sizeof(s->regs));

    s->regs[PF53_DEVICE_ID_REG] = PF53_DEVICE_ID_RESET;
    s->regs[PF53_REVISION_ID_REG] = PF53_REVISION_ID_RESET;
    s->regs[PF53_EMREV_REG] = PF53_EMREV_RESET;
    s->regs[PF53_PROGRAM_ID_REG] = PF53_PROGRAM_ID_RESET;

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
    } else {
        s->regs[s->reg_ptr++] = data;
    }

    return 0;
}

static uint8_t pf53_recv(I2CSlave *i2c)
{
    PF53PMICState *s = PF53_PMIC(i2c);

    return s->regs[s->reg_ptr++];
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
        VMSTATE_UINT8_ARRAY(regs, PF53PMICState, PF53_PMIC_REG_COUNT),
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
    .name = TYPE_PF53_PMIC,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(PF53PMICState),
    .class_init = pf53_pmic_class_init,
};

static void pf53_pmic_register_types(void)
{
    type_register_static(&pf53_pmic_info);
}

type_init(pf53_pmic_register_types)

