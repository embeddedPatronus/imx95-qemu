/*
 * NXP i.MX95 NETC IEPRC ECAM window
 *
 * ECAM window of the NETC integrated endpoints, which are not modelled:
 * configuration reads return all ones and writes are discarded.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "system/memory.h"
#include "hw/misc/imx95_netc_ecam.h"

static uint64_t imx95_netc_ecam_read(void *opaque, hwaddr offset,
                                     unsigned size)
{
    return MAKE_64BIT_MASK(0, size * 8);
}

static void imx95_netc_ecam_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned size)
{
}

static const MemoryRegionOps imx95_netc_ecam_ops = {
    .read = imx95_netc_ecam_read,
    .write = imx95_netc_ecam_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
};

static void imx95_netc_ecam_init(Object *obj)
{
    Imx95NetcEcamState *s = IMX95_NETC_ECAM(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_netc_ecam_ops, s,
                          TYPE_IMX95_NETC_ECAM, IMX95_NETC_ECAM_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const TypeInfo imx95_netc_ecam_types[] = {
    {
        .name = TYPE_IMX95_NETC_ECAM,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Imx95NetcEcamState),
        .instance_init = imx95_netc_ecam_init,
    },
};

DEFINE_TYPES(imx95_netc_ecam_types)
