/*
 * NXP i.MX95 GPIO register model
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/imx95_gpio.h"
#include "migration/vmstate.h"

static unsigned int imx95_gpio_reg_index(hwaddr offset)
{
    return offset / sizeof(uint32_t);
}

static uint32_t imx95_gpio_access_mask(hwaddr offset, unsigned int size)
{
    unsigned int shift = (offset & 3U) * 8U;

    switch (size) {
    case 1:
        return (uint32_t)UINT8_MAX << shift;
    case 2:
        return (uint32_t)UINT16_MAX << shift;
    case 4:
        return UINT32_MAX;
    default:
        return 0;
    }
}

static uint32_t imx95_gpio_extract_subword(uint32_t value, hwaddr offset,
                                           unsigned int size)
{
    unsigned int shift = (offset & 3U) * 8U;

    return (value & imx95_gpio_access_mask(offset, size)) >> shift;
}

static uint32_t imx95_gpio_merge_subword(uint32_t old_value, hwaddr offset,
                                         uint64_t value, unsigned int size)
{
    unsigned int shift = (offset & 3U) * 8U;
    uint32_t mask = imx95_gpio_access_mask(offset, size);

    return (old_value & ~mask) | (((uint32_t)value << shift) & mask);
}

/* Register file only: pins and interrupts are not modelled */
static uint64_t imx95_gpio_read(void *opaque, hwaddr offset,
                                unsigned int size)
{
    Imx95GpioState *s = opaque;
    hwaddr word_offset = offset & ~(hwaddr)3;
    uint32_t value;

    if (word_offset >= IMX95_GPIO_SIZE) {
        return 0;
    }

    value = s->regs[imx95_gpio_reg_index(word_offset)];
    return imx95_gpio_extract_subword(value, offset, size);
}

static void imx95_gpio_write(void *opaque, hwaddr offset,
                             uint64_t value, unsigned int size)
{
    Imx95GpioState *s = opaque;
    hwaddr word_offset = offset & ~(hwaddr)3;
    unsigned int index;

    if (word_offset >= IMX95_GPIO_SIZE) {
        return;
    }

    index = imx95_gpio_reg_index(word_offset);
    s->regs[index] = imx95_gpio_merge_subword(s->regs[index], offset,
                                              value, size);
}

static const MemoryRegionOps imx95_gpio_ops = {
    .read = imx95_gpio_read,
    .write = imx95_gpio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void imx95_gpio_reset(DeviceState *dev)
{
    Imx95GpioState *s = IMX95_GPIO(dev);

    memset(s->regs, 0, sizeof(s->regs));
}

static void imx95_gpio_init(Object *obj)
{
    Imx95GpioState *s = IMX95_GPIO(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_gpio_ops, s,
                          TYPE_IMX95_GPIO, IMX95_GPIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription vmstate_imx95_gpio = {
    .name = TYPE_IMX95_GPIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, Imx95GpioState,
                             IMX95_GPIO_REG_COUNT),
        VMSTATE_END_OF_LIST()
    },
};

static void imx95_gpio_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, imx95_gpio_reset);
    dc->vmsd = &vmstate_imx95_gpio;
    dc->desc = "i.MX95 GPIO register model";
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

