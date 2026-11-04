/*
 * NXP i.MX95 gpio
 *
 * Storage-backed GPIO bank. The registers hold the written value; no pin
 * behaviour is modelled.
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
#include "hw/misc/imx95_gpio.h"
#include "migration/vmstate.h"
#include "hw/core/irq.h"
#include "system/memory.h"

static uint64_t imx95_gpio_stub_read(void *opaque, hwaddr offset,
                                     unsigned size)
{
    Imx95GpioState *s = opaque;

    if (offset >= IMX95_GPIO_SIZE) {
        return 0;
    }

    return s->regs[offset / 4];
}

static void imx95_gpio_stub_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned size)
{
    Imx95GpioState *s = opaque;

    if (offset >= IMX95_GPIO_SIZE) {
        return;
    }

    s->regs[offset / 4] = (uint32_t)value;
}

static const MemoryRegionOps imx95_gpio_stub_ops = {
    .read = imx95_gpio_stub_read,
    .write = imx95_gpio_stub_write,
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

static void imx95_gpio_reset(DeviceState *dev)
{
    Imx95GpioState *s = IMX95_GPIO(dev);
    memset(s->regs, 0, sizeof(s->regs));
    /* Bank input/status register at 0x54 is storage-backed; reset value 0. */
}

static void imx95_gpio_init(Object *obj)
{
    Imx95GpioState *s = IMX95_GPIO(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_gpio_stub_ops, s,
                          TYPE_IMX95_GPIO, IMX95_GPIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription vmstate_imx95_gpio = {
    .name = TYPE_IMX95_GPIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, Imx95GpioState, IMX95_GPIO_SIZE / 4),
        VMSTATE_END_OF_LIST()
    }
};

static void imx95_gpio_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, imx95_gpio_reset);
    dc->vmsd = &vmstate_imx95_gpio;
    dc->desc = "i.MX95 gpio";
}

static const TypeInfo imx95_gpio_types[] = {
    {
        .name = TYPE_IMX95_GPIO,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Imx95GpioState),
        .instance_init = imx95_gpio_init,
        .class_init = imx95_gpio_class_init,
    },
};

DEFINE_TYPES(imx95_gpio_types)
