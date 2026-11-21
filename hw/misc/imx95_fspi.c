/*
 * NXP i.MX95 FlexSPI register model
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "hw/misc/imx95_fspi.h"
#include "migration/vmstate.h"

#define IMX95_FSPI_MCR0          0x000
#define IMX95_FSPI_MCR0_SWRST    BIT(0)
#define IMX95_FSPI_INTR          0x014
#define IMX95_FSPI_INTR_IPRXWA   BIT(5)
#define IMX95_FSPI_INTR_IPTXWE   BIT(6)
#define IMX95_FSPI_STS0          0x0e0
#define IMX95_FSPI_STS0_ARB_IDLE BIT(1)
#define IMX95_FSPI_STS2          0x0e8
#define IMX95_FSPI_STS2_AB_LOCK  (BIT(17) | BIT(16))
#define IMX95_FSPI_RFDR          0x100
#define IMX95_FSPI_RFDR_END      0x180

static unsigned int imx95_fspi_reg_index(hwaddr offset)
{
    return offset / sizeof(uint32_t);
}

static uint32_t imx95_fspi_access_mask(hwaddr offset, unsigned int size)
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

static uint32_t imx95_fspi_extract_subword(uint32_t value, hwaddr offset,
                                           unsigned int size)
{
    unsigned int shift = (offset & 3U) * 8U;

    return (value & imx95_fspi_access_mask(offset, size)) >> shift;
}

static uint32_t imx95_fspi_merge_subword(uint32_t old_value, hwaddr offset,
                                         uint64_t value, unsigned int size)
{
    unsigned int shift = (offset & 3U) * 8U;
    uint32_t mask = imx95_fspi_access_mask(offset, size);

    return (old_value & ~mask) | (((uint32_t)value << shift) & mask);
}

/* Register stub: no LUT or flash behind the controller */
static uint32_t imx95_fspi_read_word(Imx95FspiState *s,
                                     hwaddr word_offset)
{
    switch (word_offset) {
    case IMX95_FSPI_MCR0:
        return s->regs[imx95_fspi_reg_index(word_offset)] &
               ~IMX95_FSPI_MCR0_SWRST;
    case IMX95_FSPI_STS0:
        return IMX95_FSPI_STS0_ARB_IDLE;
    case IMX95_FSPI_STS2:
        return IMX95_FSPI_STS2_AB_LOCK;
    case IMX95_FSPI_INTR:
        return IMX95_FSPI_INTR_IPTXWE | IMX95_FSPI_INTR_IPRXWA;
    default:
        /* The RX FIFO reads as all-ones */
        if (word_offset >= IMX95_FSPI_RFDR &&
            word_offset < IMX95_FSPI_RFDR_END) {
            return UINT32_MAX;
        }
        return s->regs[imx95_fspi_reg_index(word_offset)];
    }
}

static uint64_t imx95_fspi_read(void *opaque, hwaddr offset,
                                unsigned int size)
{
    Imx95FspiState *s = opaque;
    hwaddr word_offset = offset & ~(hwaddr)3;
    uint32_t value;

    if (word_offset >= IMX95_FSPI_REG_SIZE) {
        return 0;
    }

    value = imx95_fspi_read_word(s, word_offset);
    return imx95_fspi_extract_subword(value, offset, size);
}

static void imx95_fspi_write(void *opaque, hwaddr offset,
                             uint64_t value, unsigned int size)
{
    Imx95FspiState *s = opaque;
    hwaddr word_offset = offset & ~(hwaddr)3;
    unsigned int index;

    if (word_offset >= IMX95_FSPI_REG_SIZE ||
        word_offset == IMX95_FSPI_INTR) {
        return;
    }

    index = imx95_fspi_reg_index(word_offset);
    s->regs[index] = imx95_fspi_merge_subword(s->regs[index], offset,
                                              value, size);
}

static const MemoryRegionOps imx95_fspi_ops = {
    .read = imx95_fspi_read,
    .write = imx95_fspi_write,
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

static void imx95_fspi_reset(DeviceState *dev)
{
    Imx95FspiState *s = IMX95_FSPI(dev);

    memset(s->regs, 0, sizeof(s->regs));
}

static void imx95_fspi_init(Object *obj)
{
    Imx95FspiState *s = IMX95_FSPI(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_fspi_ops, s,
                          TYPE_IMX95_FSPI, IMX95_FSPI_REG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription vmstate_imx95_fspi = {
    .name = TYPE_IMX95_FSPI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, Imx95FspiState,
                             IMX95_FSPI_REG_COUNT),
        VMSTATE_END_OF_LIST()
    },
};

static void imx95_fspi_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, imx95_fspi_reset);
    dc->vmsd = &vmstate_imx95_fspi;
    dc->desc = "i.MX95 FlexSPI register model";
}

static const TypeInfo imx95_fspi_types[] = {
    {
        .name = TYPE_IMX95_FSPI,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Imx95FspiState),
        .instance_init = imx95_fspi_init,
        .class_init = imx95_fspi_class_init,
    },
};

DEFINE_TYPES(imx95_fspi_types)

