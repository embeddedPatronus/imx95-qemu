/*
 * NXP i.MX95 wdog32
 *
 * Register-level model of the 32-bit watchdog (CS, CNT, TOVAL and WIN). It
 * accepts the 16-bit and the 32-bit unlock sequences and its status bits
 * always read as ready; the timeout is not counted down.
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
#include "hw/misc/imx95_wdog32.h"
#include "migration/vmstate.h"
#include "hw/core/irq.h"
#include "system/memory.h"

#define WDOG32_CS        0x00
#define WDOG32_CNT       0x04
#define WDOG32_TOVAL     0x08
#define WDOG32_WIN       0x0C

#define WDOG32_CS_UPDATE BIT(5)
#define WDOG32_CS_RCS    BIT(10)
#define WDOG32_CS_ULK   BIT(11)
#define WDOG32_CS_CMD32  BIT(13)


static uint64_t imx95_wdog32_read(void *opaque, hwaddr offset, unsigned size)
{
    Imx95Wdog32State *s = opaque;

    switch (offset) {
    case WDOG32_CS:
           return s->cs | WDOG32_CS_UPDATE | WDOG32_CS_ULK | WDOG32_CS_RCS | WDOG32_CS_CMD32;

    case WDOG32_CNT:
        return s->cnt;

    case WDOG32_TOVAL:
        return s->toval;

    case WDOG32_WIN:
       return s->win;

    default:
       return 0;
    }
}

static void imx95_wdog32_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned size)
{
    Imx95Wdog32State *s = opaque;
    uint32_t v = value;

   switch (offset) {
    case WDOG32_CS:
        s->cs = v | WDOG32_CS_UPDATE | WDOG32_CS_ULK | WDOG32_CS_RCS | WDOG32_CS_CMD32;
        break;

    case WDOG32_CNT:
       s->cnt = v;

        if ((v & 0xffff) == 0xc520) {
            s->unlock_seen_first = true;
        } else if (s->unlock_seen_first && ((v & 0xffff) == 0xd928)) {
            s->unlock_seen_first = false;
            s->cs |= WDOG32_CS_UPDATE | WDOG32_CS_ULK | WDOG32_CS_RCS | WDOG32_CS_CMD32;
        } else if (v == 0xd928c520) {
            s->cs |= WDOG32_CS_UPDATE | WDOG32_CS_ULK | WDOG32_CS_RCS | WDOG32_CS_CMD32;
        }
        break;

    case WDOG32_TOVAL:
        s->toval = v;
        break;

    case WDOG32_WIN:
        s->win = v;
        break;

    default:
        break;
    }
}

static const MemoryRegionOps imx95_wdog32_ops = {
    .read = imx95_wdog32_read,
    .write = imx95_wdog32_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void imx95_wdog32_reset(DeviceState *dev)
{
    Imx95Wdog32State *s = IMX95_WDOG32(dev);
    s->cs = 0;
    s->cnt = 0;
    s->toval = 0;
    s->win = 0;
    s->unlock_seen_first = 0;


    s->cs = WDOG32_CS_UPDATE | WDOG32_CS_ULK | WDOG32_CS_RCS | WDOG32_CS_CMD32;
    s->toval = 0xffff;
    s->win = 0;
}

static void imx95_wdog32_init(Object *obj)
{
    Imx95Wdog32State *s = IMX95_WDOG32(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_wdog32_ops, s,
                          TYPE_IMX95_WDOG32, IMX95_WDOG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription vmstate_imx95_wdog32 = {
    .name = TYPE_IMX95_WDOG32,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(cs, Imx95Wdog32State),
        VMSTATE_UINT32(cnt, Imx95Wdog32State),
        VMSTATE_UINT32(toval, Imx95Wdog32State),
        VMSTATE_UINT32(win, Imx95Wdog32State),
        VMSTATE_BOOL(unlock_seen_first, Imx95Wdog32State),
        VMSTATE_END_OF_LIST()
    }
};

static void imx95_wdog32_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, imx95_wdog32_reset);
    dc->vmsd = &vmstate_imx95_wdog32;
    dc->desc = "i.MX95 wdog32";
}

static const TypeInfo imx95_wdog32_types[] = {
    {
        .name = TYPE_IMX95_WDOG32,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Imx95Wdog32State),
        .instance_init = imx95_wdog32_init,
        .class_init = imx95_wdog32_class_init,
    },
};

DEFINE_TYPES(imx95_wdog32_types)
