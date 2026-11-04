/*
 * NXP i.MX95 fspi
 *
 * Minimal FlexSPI controller (flexspi1). The status and handshake bits
 * that the Linux driver polls read as ready, so the driver does not time
 * out. No flash device is modelled.
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
#include "hw/misc/imx95_fspi.h"
#include "migration/vmstate.h"
#include "hw/core/irq.h"
#include "system/memory.h"

#define IMX95_FSPI1_MMAP_SIZE 0x08000000

#define IMX95_FSPI_MCR0        0x00
#define IMX95_FSPI_MCR0_SWRST  (1U << 0)
#define IMX95_FSPI_STS0        0xE0
#define IMX95_FSPI_STS0_ARB_IDLE (1U << 1)
#define IMX95_FSPI_STS2        0xE8
#define IMX95_FSPI_STS2_AB_LOCK  ((1U << 17) | (1U << 16))
#define IMX95_FSPI_INTR        0x14
#define IMX95_FSPI_INTR_IPTXWE (1U << 6)
#define IMX95_FSPI_INTR_IPRXWA (1U << 5)
#define IMX95_FSPI_RFDR        0x100
#define IMX95_FSPI_RFDR_END    0x180


static uint64_t imx95_fspi_read(void *opaque, hwaddr offset, unsigned size)
{
    Imx95FspiState *s = opaque;
    hwaddr word_offset = offset & ~3ULL;

    if (word_offset >= IMX95_FSPI1_REG_SIZE) {
        return 0;
    }

    switch (word_offset) {
    case IMX95_FSPI_MCR0:
        return s->regs[word_offset / 4] & ~IMX95_FSPI_MCR0_SWRST;

    case IMX95_FSPI_STS0:
        return IMX95_FSPI_STS0_ARB_IDLE;

    case IMX95_FSPI_STS2:
        return IMX95_FSPI_STS2_AB_LOCK;

    case IMX95_FSPI_INTR:
        return IMX95_FSPI_INTR_IPTXWE | IMX95_FSPI_INTR_IPRXWA;

    default:
        if (word_offset >= IMX95_FSPI_RFDR &&
            word_offset < IMX95_FSPI_RFDR_END) {
            return 0xffffffffu;
        }
        return s->regs[word_offset / 4];
    }
}

static void imx95_fspi_write(void *opaque, hwaddr offset,
                             uint64_t value, unsigned size)
{
    Imx95FspiState *s = opaque;
    hwaddr word_offset = offset & ~3ULL;

    if (word_offset >= IMX95_FSPI1_REG_SIZE) {
        return;
    }

    if (word_offset == IMX95_FSPI_INTR) {
        return;
    }

    s->regs[word_offset / 4] = (uint32_t)value;
}

static const MemoryRegionOps imx95_fspi_ops = {
    .read = imx95_fspi_read,
    .write = imx95_fspi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
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
                          TYPE_IMX95_FSPI, IMX95_FSPI1_REG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription vmstate_imx95_fspi = {
    .name = TYPE_IMX95_FSPI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, Imx95FspiState, IMX95_FSPI1_REG_SIZE / sizeof(uint32_t)),
        VMSTATE_END_OF_LIST()
    }
};

static void imx95_fspi_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, imx95_fspi_reset);
    dc->vmsd = &vmstate_imx95_fspi;
    dc->desc = "i.MX95 fspi";
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
