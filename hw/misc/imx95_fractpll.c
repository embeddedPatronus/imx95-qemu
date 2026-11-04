/*
 * NXP i.MX95 fractpll
 *
 * Fractional PLL with its DFS outputs. Control registers store the written
 * value and honour the SET/CLR/TOG aliases, PLL_STATUS always reports lock
 * and DFS_STATUS reports every DFS output as ready. The reset state
 * reproduces the SYS_PLL1 setup the boot ROM leaves for the System
 * Manager.
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
#include "hw/misc/imx95_fractpll.h"
#include "migration/vmstate.h"
#include "hw/core/irq.h"
#include "system/memory.h"

#define IMX95_FRACTPLL_NUMERATOR_OFFSET 0x040
#define IMX95_FRACTPLL_DENOMINATOR_OFFSET 0x050
#define IMX95_FRACTPLL_DIV_OFFSET 0x060
#define IMX95_FRACTPLL_DFS0_DIV_OFFSET 0x080
#define IMX95_FRACTPLL_DFS1_DIV_OFFSET 0x0a0
#define IMX95_FRACTPLL_DFS2_DIV_OFFSET 0x0c0

#define IMX95_FRACTPLL_STATUS_OFFSET 0x0f0
#define IMX95_FRACTPLL_DFS_STATUS_OFFSET 0x0f4
#define IMX95_FRACTPLL_BLOCK_SIZE    0x100

#define IMX95_FRACTPLL_STATUS_LOCK   BIT(0)
#define IMX95_FRACTPLL_DFS_ALL_OK    0xffffffffu

#define IMX95_SYSPLL1_NUMERATOR_RESET    8U
#define IMX95_SYSPLL1_DENOMINATOR_RESET  3U
#define IMX95_SYSPLL1_DIV_RESET          ((166U << 16) | (1U << 13))
#define IMX95_SYSPLL1_DFS0_DIV_RESET     (4U << 8)
#define IMX95_SYSPLL1_DFS1_DIV_RESET     (5U << 8)
#define IMX95_SYSPLL1_DFS2_DIV_RESET     (6U << 8)



static uint64_t imx95_fractpll_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    Imx95FractpllState *s = opaque;
    uint32_t value;
    hwaddr block_off;

    if (offset >= IMX95_FRACTPLL_SIZE) {
        return 0;
    }

    value = s->regs[offset / 4];

    block_off = offset % IMX95_FRACTPLL_BLOCK_SIZE;

if (block_off == IMX95_FRACTPLL_STATUS_OFFSET) {
value |= IMX95_FRACTPLL_STATUS_LOCK;
}

if (block_off == IMX95_FRACTPLL_DFS_STATUS_OFFSET) {
value |= IMX95_FRACTPLL_DFS_ALL_OK;
}
    return value;
}

static void imx95_fractpll_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size)
{
    Imx95FractpllState *s = opaque;
    hwaddr block_off;
    hwaddr rw_index;
    unsigned sub;

    if (offset >= IMX95_FRACTPLL_SIZE) {
        return;
    }

    block_off = offset % IMX95_FRACTPLL_BLOCK_SIZE;

    if (block_off < IMX95_FRACTPLL_STATUS_OFFSET) {
        sub = (block_off >> 2) & 0x3U;
        rw_index = (offset & ~(hwaddr)0xFU) / 4U;

        switch (sub) {
        case 0: /* RW */
            s->regs[rw_index] = value;
            break;
        case 1: /* SET */
            s->regs[rw_index] |= value;
            break;
        case 2: /* CLR */
            s->regs[rw_index] &= ~(uint32_t)value;
            break;
        default: /* TOG */
            s->regs[rw_index] ^= value;
            break;
        }
        return;
    }

    s->regs[offset / 4] = value;
}

static const MemoryRegionOps imx95_fractpll_ops = {
    .read = imx95_fractpll_read,
    .write = imx95_fractpll_write,
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

static void imx95_fractpll_reset(DeviceState *dev)
{
    Imx95FractpllState *s = IMX95_FRACTPLL(dev);
    memset(s->regs, 0, sizeof(s->regs));

    s->regs[IMX95_FRACTPLL_NUMERATOR_OFFSET / sizeof(uint32_t)] =
        IMX95_SYSPLL1_NUMERATOR_RESET;
    s->regs[IMX95_FRACTPLL_DENOMINATOR_OFFSET / sizeof(uint32_t)] =
       IMX95_SYSPLL1_DENOMINATOR_RESET;
    s->regs[IMX95_FRACTPLL_DIV_OFFSET / sizeof(uint32_t)] =
        IMX95_SYSPLL1_DIV_RESET;
    s->regs[IMX95_FRACTPLL_DFS0_DIV_OFFSET / sizeof(uint32_t)] =
        IMX95_SYSPLL1_DFS0_DIV_RESET;
    s->regs[IMX95_FRACTPLL_DFS1_DIV_OFFSET / sizeof(uint32_t)] =
        IMX95_SYSPLL1_DFS1_DIV_RESET;
    s->regs[IMX95_FRACTPLL_DFS2_DIV_OFFSET / sizeof(uint32_t)] =
        IMX95_SYSPLL1_DFS2_DIV_RESET;
}

static void imx95_fractpll_init(Object *obj)
{
    Imx95FractpllState *s = IMX95_FRACTPLL(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_fractpll_ops, s,
                          TYPE_IMX95_FRACTPLL, IMX95_FRACTPLL_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription vmstate_imx95_fractpll = {
    .name = TYPE_IMX95_FRACTPLL,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, Imx95FractpllState, IMX95_FRACTPLL_SIZE / 4),
        VMSTATE_END_OF_LIST()
    }
};

static void imx95_fractpll_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, imx95_fractpll_reset);
    dc->vmsd = &vmstate_imx95_fractpll;
    dc->desc = "i.MX95 fractpll";
}

static const TypeInfo imx95_fractpll_types[] = {
    {
        .name = TYPE_IMX95_FRACTPLL,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Imx95FractpllState),
        .instance_init = imx95_fractpll_init,
        .class_init = imx95_fractpll_class_init,
    },
};

DEFINE_TYPES(imx95_fractpll_types)
