/*
 * NXP i.MX95 BBNSM model
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/imx95_bbnsm.h"
#include "migration/vmstate.h"

#define BBNSM_VID       0x000
#define BBNSM_FEATURES  0x004
#define BBNSM_CTRL      0x008
#define BBNSM_INT_EN    0x010
#define BBNSM_EVENTS    0x014
#define BBNSM_PAD_CTRL  0x024
#define BBNSM_RTC_LS    0x040
#define BBNSM_RTC_MS    0x044
#define BBNSM_TA        0x050
#define BBNSM_GPR_BASE  0x300
#define BBNSM_GPR_END   0x320

#define BBNSM_FEATURES_GPR_SZ (8U << 2)
#define BBNSM_VID_RESET       0x00010001

static unsigned int imx95_bbnsm_reg_index(hwaddr offset)
{
    return offset / sizeof(uint32_t);
}

static uint32_t imx95_bbnsm_access_mask(hwaddr offset,
                                        unsigned int size)
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

static uint32_t imx95_bbnsm_extract_subword(uint32_t value,
                                            hwaddr offset,
                                            unsigned int size)
{
    unsigned int shift = (offset & 3U) * 8U;

    return (value & imx95_bbnsm_access_mask(offset, size)) >> shift;
}

static uint32_t imx95_bbnsm_merge_subword(uint32_t old_value,
                                          hwaddr offset,
                                          uint64_t value,
                                          unsigned int size)
{
    unsigned int shift = (offset & 3U) * 8U;
    uint32_t mask = imx95_bbnsm_access_mask(offset, size);

    return (old_value & ~mask) | (((uint32_t)value << shift) & mask);
}

static uint64_t imx95_bbnsm_read(void *opaque, hwaddr offset,
                                 unsigned int size)
{
    Imx95BbnsmState *s = opaque;
    hwaddr word_offset = offset & ~(hwaddr)3;
    uint32_t value;

    if (word_offset >= IMX95_BBNSM_SIZE) {
        return 0;
    }

    value = s->regs[imx95_bbnsm_reg_index(word_offset)];
    s->read_count++;

    return imx95_bbnsm_extract_subword(value, offset, size);
}

static void imx95_bbnsm_write(void *opaque, hwaddr offset,
                               uint64_t value, unsigned int size)
{
    Imx95BbnsmState *s = opaque;
    hwaddr word_offset = offset & ~(hwaddr)3;
    unsigned int index;
    uint32_t old_value;
    uint32_t new_value;
    uint32_t write_mask;

    if (word_offset >= IMX95_BBNSM_SIZE) {
        return;
    }

    index = imx95_bbnsm_reg_index(word_offset);
    old_value = s->regs[index];
    new_value = imx95_bbnsm_merge_subword(old_value, offset, value, size);
    write_mask = imx95_bbnsm_access_mask(offset, size);
    s->write_count++;

    switch (word_offset) {
    case BBNSM_VID:
    case BBNSM_FEATURES:
        break;
    /* Write-one-to-clear */
    case BBNSM_EVENTS:
        s->regs[index] &= ~(new_value & write_mask);
        break;
    default:
        s->regs[index] = new_value;
        break;
    }
}

static const MemoryRegionOps imx95_bbnsm_ops = {
    .read = imx95_bbnsm_read,
    .write = imx95_bbnsm_write,
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

/* The RTC and alarm are not modelled */
static void imx95_bbnsm_reset(DeviceState *dev)
{
    Imx95BbnsmState *s = IMX95_BBNSM(dev);

    memset(s->regs, 0, sizeof(s->regs));
    s->read_count = 0;
    s->write_count = 0;
    s->regs[imx95_bbnsm_reg_index(BBNSM_FEATURES)] =
        BBNSM_FEATURES_GPR_SZ;
    s->regs[imx95_bbnsm_reg_index(BBNSM_VID)] = BBNSM_VID_RESET;
}

static void imx95_bbnsm_init(Object *obj)
{
    Imx95BbnsmState *s = IMX95_BBNSM(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_bbnsm_ops, s,
                          TYPE_IMX95_BBNSM, IMX95_BBNSM_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_imx95_bbnsm = {
    .name = TYPE_IMX95_BBNSM,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, Imx95BbnsmState,
                             IMX95_BBNSM_REG_COUNT),
        VMSTATE_UINT64(read_count, Imx95BbnsmState),
        VMSTATE_UINT64(write_count, Imx95BbnsmState),
        VMSTATE_END_OF_LIST()
    },
};

static void imx95_bbnsm_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, imx95_bbnsm_reset);
    dc->vmsd = &vmstate_imx95_bbnsm;
    dc->desc = "i.MX95 BBNSM";
}

static const TypeInfo imx95_bbnsm_types[] = {
    {
        .name = TYPE_IMX95_BBNSM,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Imx95BbnsmState),
        .instance_init = imx95_bbnsm_init,
        .class_init = imx95_bbnsm_class_init,
    },
};

DEFINE_TYPES(imx95_bbnsm_types)

