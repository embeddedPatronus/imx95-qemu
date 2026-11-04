/*
 * NXP i.MX95 bbnsm
 *
 * Battery-backed non-secure module: the ID and feature registers are read-
 * only, the event register clears the bits written as one, and the
 * control, RTC and general purpose registers hold the written value.
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
#include "hw/misc/imx95_bbnsm.h"
#include "migration/vmstate.h"
#include "hw/core/irq.h"
#include "system/memory.h"

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


static uint32_t imx95_extract_subword(uint32_t value, hwaddr byte_offset,
                                      unsigned size)
{
    unsigned shift = (byte_offset & 3U) * 8U;

    switch (size) {
    case 1:
        return (value >> shift) & UINT8_MAX;
    case 2:
        return (value >> shift) & UINT16_MAX;
    case 4:
        return value;
    default:
        return 0;
    }
}

static uint64_t imx95_bbnsm_read(void *opaque, hwaddr offset, unsigned size)
{
    Imx95BbnsmState *s = opaque;
    hwaddr word_offset = offset & ~3ULL;
    uint32_t value;

    if (word_offset >= IMX95_BBNSM_SIZE) {
        return 0;
    }

    value = s->regs[word_offset / 4];

    switch (word_offset) {
    case BBNSM_VID:
    case BBNSM_FEATURES:
    case BBNSM_CTRL:
    case BBNSM_INT_EN:
    case BBNSM_EVENTS:
        return s->regs[offset / 4];
    case BBNSM_PAD_CTRL:
    case BBNSM_RTC_LS:
    case BBNSM_RTC_MS:
    case BBNSM_TA:
    default:
	break;
    }
     s->read_count++;

    if (s->read_count <= 256 ||
        word_offset == BBNSM_VID ||
        word_offset == BBNSM_FEATURES ||
        word_offset == BBNSM_CTRL ||
        word_offset == BBNSM_EVENTS ||
        (word_offset >= BBNSM_GPR_BASE &&
         word_offset < BBNSM_GPR_END)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.bbnsm: read count=%" PRIu64
                      " offset=0x%04" HWADDR_PRIx
                      " size=%u value=0x%08x\n",
                      s->read_count,
                      offset,
                      size,
                      value);
    }

    return imx95_extract_subword(value, offset, size);
}

static void imx95_bbnsm_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned size)
{
    Imx95BbnsmState *s = opaque;
    hwaddr word_offset = offset & ~3ULL;
    unsigned shift;
    uint32_t mask;
    uint32_t old_value;
    uint32_t new_value;

    if (word_offset >= IMX95_BBNSM_SIZE) {
        return;
    }

    old_value = s->regs[word_offset / 4];

    switch (size) {
    case 1:
        shift = (offset & 3U) * 8U;
        mask = UINT8_MAX << shift;
        new_value = (old_value & ~mask) |
                    ((((uint32_t)value) << shift) & mask);
        break;
    case 2:
        shift = (offset & 2U) * 8U;
        mask = UINT16_MAX << shift;
        new_value = (old_value & ~mask) |
                    ((((uint32_t)value) << shift) & mask);
        break;
    case 4:
        new_value = (uint32_t)value;
        break;
    default:
        return;
    }

    s->write_count++;

    qemu_log_mask(LOG_GUEST_ERROR,
                  "imx95.bbnsm: write count=%" PRIu64
                  " offset=0x%04" HWADDR_PRIx
                  " size=%u old=0x%08x value=0x%08" PRIx64
                  " new=0x%08x\n",
                  s->write_count,
                  offset,
                  size,
                  old_value,
                  value,
                  new_value);

    switch (word_offset) {
    case BBNSM_VID:
    case BBNSM_FEATURES:
        break;
    case BBNSM_EVENTS:
        s->regs[word_offset / 4] &= ~new_value;
        break;

    default:
        s->regs[word_offset / 4] = new_value;
        break;
    }
}

static const MemoryRegionOps imx95_bbnsm_ops = {
    .read = imx95_bbnsm_read,
    .write = imx95_bbnsm_write,
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

static void imx95_bbnsm_reset(DeviceState *dev)
{
    Imx95BbnsmState *s = IMX95_BBNSM(dev);
    memset(s->regs, 0, sizeof(s->regs));
    s->read_count = 0;
    s->write_count = 0;


    s->regs[BBNSM_FEATURES / 4] = BBNSM_FEATURES_GPR_SZ;
    s->regs[BBNSM_VID / 4] = 0x00010001;
}

static void imx95_bbnsm_init(Object *obj)
{
    Imx95BbnsmState *s = IMX95_BBNSM(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_bbnsm_ops, s,
                          TYPE_IMX95_BBNSM, IMX95_BBNSM_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription vmstate_imx95_bbnsm = {
    .name = TYPE_IMX95_BBNSM,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, Imx95BbnsmState, IMX95_BBNSM_SIZE / 4),
        VMSTATE_UINT64(read_count, Imx95BbnsmState),
        VMSTATE_UINT64(write_count, Imx95BbnsmState),
        VMSTATE_END_OF_LIST()
    }
};

static void imx95_bbnsm_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, imx95_bbnsm_reset);
    dc->vmsd = &vmstate_imx95_bbnsm;
    dc->desc = "i.MX95 bbnsm";
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
