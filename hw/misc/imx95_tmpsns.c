/*
 * NXP i.MX95 tmpsns
 *
 * Temperature sensor register block. The registers store the written
 * value, and the SET/CLR/TOG aliases at offsets 4, 8 and 0xc change the
 * register in place.
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
#include "hw/misc/imx95_tmpsns.h"
#include "migration/vmstate.h"
#include "hw/core/irq.h"
#include "system/memory.h"

static uint32_t imx95_tmpsns_get_word(Imx95TmpsnsState *s,
                                      hwaddr word_offset)
{
    return s->regs[word_offset / sizeof(uint32_t)];
}

static void imx95_tmpsns_set_word(Imx95TmpsnsState *s,
                                  hwaddr word_offset,
                                  uint32_t value)
{
    s->regs[word_offset / sizeof(uint32_t)] = value;
}

static bool imx95_tmpsns_alias_write(Imx95TmpsnsState *s,
                                     hwaddr word_offset,
                                     uint32_t value)
{
    hwaddr group_offset = word_offset & 0xf;
    hwaddr base_offset = word_offset - group_offset;
    uint32_t current;

    if (group_offset != 0x4 &&
        group_offset != 0x8 &&
        group_offset != 0xc) {
        return false;
    }

    current = imx95_tmpsns_get_word(s, base_offset);

    switch (group_offset) {
    case 0x4:
        current |= value;
        break;
    case 0x8:
        current &= ~value;
        break;
    case 0xc:
        current ^= value;
        break;
    default:
        return false;
    }

    imx95_tmpsns_set_word(s, base_offset, current);
    return true;
}

static uint64_t imx95_tmpsns_read(void *opaque,
                                  hwaddr offset,
                                  unsigned size)
{
    Imx95TmpsnsState *s = opaque;
    hwaddr word_offset = offset & ~3ULL;
    hwaddr byte_offset = offset & 3ULL;
    uint32_t value;

    if (word_offset >= IMX95_TMPSNS_SIZE) {
        return 0;
    }

    value = imx95_tmpsns_get_word(s, word_offset);

    switch (size) {
    case 1:
        return (value >> (byte_offset * 8)) & UINT8_MAX;
    case 2:
        return (value >> (byte_offset * 8)) & UINT16_MAX;
    case 4:
        return value;
    default:
        return 0;
    }
}

static void imx95_tmpsns_write(void *opaque,
                               hwaddr offset,
                               uint64_t value,
                               unsigned size)
{
    Imx95TmpsnsState *s = opaque;
    hwaddr word_offset = offset & ~3ULL;
    hwaddr byte_offset = offset & 3ULL;
    uint32_t old_value;
    uint32_t new_value;
    uint32_t shift;
    uint32_t mask;

    if (word_offset >= IMX95_TMPSNS_SIZE) {
        return;
    }

    old_value = imx95_tmpsns_get_word(s, word_offset);

    switch (size) {
    case 1:
        shift = byte_offset * 8;
        mask = UINT8_MAX << shift;
        new_value = (old_value & ~mask) |
                    (((uint32_t)value << shift) & mask);
        break;
    case 2:
        shift = byte_offset * 8;
        mask = UINT16_MAX << shift;
        new_value = (old_value & ~mask) |
                    (((uint32_t)value << shift) & mask);
        break;
    case 4:
        new_value = (uint32_t)value;
        break;
    default:
        return;
    }

    if (!imx95_tmpsns_alias_write(s, word_offset, new_value)) {
        imx95_tmpsns_set_word(s, word_offset, new_value);
    }
}

static const MemoryRegionOps imx95_tmpsns_ops = {
    .read = imx95_tmpsns_read,
    .write = imx95_tmpsns_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
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

static void imx95_tmpsns_reset(DeviceState *dev)
{
    Imx95TmpsnsState *s = IMX95_TMPSNS(dev);
    memset(s->regs, 0, sizeof(s->regs));
}

static void imx95_tmpsns_init(Object *obj)
{
    Imx95TmpsnsState *s = IMX95_TMPSNS(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_tmpsns_ops, s,
                          TYPE_IMX95_TMPSNS, IMX95_TMPSNS_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription vmstate_imx95_tmpsns = {
    .name = TYPE_IMX95_TMPSNS,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, Imx95TmpsnsState, IMX95_TMPSNS_SIZE / sizeof(uint32_t)),
        VMSTATE_END_OF_LIST()
    }
};

static void imx95_tmpsns_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, imx95_tmpsns_reset);
    dc->vmsd = &vmstate_imx95_tmpsns;
    dc->desc = "i.MX95 tmpsns";
}

static const TypeInfo imx95_tmpsns_types[] = {
    {
        .name = TYPE_IMX95_TMPSNS,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Imx95TmpsnsState),
        .instance_init = imx95_tmpsns_init,
        .class_init = imx95_tmpsns_class_init,
    },
};

DEFINE_TYPES(imx95_tmpsns_types)
