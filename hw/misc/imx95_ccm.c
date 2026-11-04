/*
 * NXP i.MX95 ccm
 *
 * Clock control module stub. Registers store what is written and read it
 * back, so the SPL clock setup, which verifies its writes, succeeds.
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
#include "hw/misc/imx95_ccm.h"
#include "migration/vmstate.h"
#include "hw/core/irq.h"
#include "system/memory.h"

static uint64_t imx95_ccm_read(void *opaque, hwaddr offset, unsigned size)
{
    Imx95CcmState *s = opaque;
    if (offset >= IMX95_CCM_SIZE) return 0;
    return s->regs[offset / 4];
}

static void imx95_ccm_write(void *opaque, hwaddr offset,
                            uint64_t value, unsigned size)
{
    Imx95CcmState *s = opaque;
    if (offset >= IMX95_CCM_SIZE) return;
    s->regs[offset / 4] = (uint32_t)value;
}

static const MemoryRegionOps imx95_ccm_ops = {
    .read = imx95_ccm_read,
    .write = imx95_ccm_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid   = { .min_access_size = 1, .max_access_size = 4 },
    .impl    = { .min_access_size = 4, .max_access_size = 4 },
};

static void imx95_ccm_reset(DeviceState *dev)
{
    Imx95CcmState *s = IMX95_CCM(dev);
    memset(s->regs, 0, sizeof(s->regs));
}

static void imx95_ccm_init(Object *obj)
{
    Imx95CcmState *s = IMX95_CCM(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_ccm_ops, s,
                          TYPE_IMX95_CCM, IMX95_CCM_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription vmstate_imx95_ccm = {
    .name = TYPE_IMX95_CCM,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, Imx95CcmState, IMX95_CCM_SIZE / 4),
        VMSTATE_END_OF_LIST()
    }
};

static void imx95_ccm_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, imx95_ccm_reset);
    dc->vmsd = &vmstate_imx95_ccm;
    dc->desc = "i.MX95 ccm";
}

static const TypeInfo imx95_ccm_types[] = {
    {
        .name = TYPE_IMX95_CCM,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Imx95CcmState),
        .instance_init = imx95_ccm_init,
        .class_init = imx95_ccm_class_init,
    },
};

DEFINE_TYPES(imx95_ccm_types)
