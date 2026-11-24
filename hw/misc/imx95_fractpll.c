/*
 * NXP i.MX95 fractional PLL register model
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "hw/misc/imx95_fractpll.h"
#include "migration/vmstate.h"

#define IMX95_FRACTPLL_NUMERATOR_OFFSET   0x040
#define IMX95_FRACTPLL_DENOMINATOR_OFFSET 0x050
#define IMX95_FRACTPLL_DIV_OFFSET         0x060
#define IMX95_FRACTPLL_DFS0_DIV_OFFSET    0x080
#define IMX95_FRACTPLL_DFS1_DIV_OFFSET    0x0a0
#define IMX95_FRACTPLL_DFS2_DIV_OFFSET    0x0c0
#define IMX95_FRACTPLL_STATUS_OFFSET      0x0f0
#define IMX95_FRACTPLL_DFS_STATUS_OFFSET  0x0f4
#define IMX95_FRACTPLL_BLOCK_SIZE         0x100

#define IMX95_FRACTPLL_STATUS_LOCK BIT(0)

#define IMX95_FRACTPLL_DRAM_PLL_OFFSET  0x700
#define IMX95_FRACTPLL_CTRL_POWERUP     BIT(0)
#define IMX95_FRACTPLL_CTRL_CLKMUX_EN   BIT(1)
#define IMX95_FRACTPLL_DFS_ALL_OK  UINT32_MAX

#define IMX95_SYSPLL1_NUMERATOR_RESET   8U
#define IMX95_SYSPLL1_DENOMINATOR_RESET 3U
#define IMX95_SYSPLL1_DIV_RESET         ((166U << 16) | (1U << 13))
#define IMX95_SYSPLL1_DFS0_DIV_RESET    (4U << 8)
#define IMX95_SYSPLL1_DFS1_DIV_RESET    (5U << 8)
#define IMX95_SYSPLL1_DFS2_DIV_RESET    (6U << 8)

static unsigned int imx95_fractpll_reg_index(hwaddr offset)
{
    return offset / sizeof(uint32_t);
}

static uint32_t imx95_fractpll_access_mask(hwaddr offset,
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

static uint32_t imx95_fractpll_extract_subword(uint32_t value,
                                               hwaddr offset,
                                               unsigned int size)
{
    unsigned int shift = (offset & 3U) * 8U;

    return (value & imx95_fractpll_access_mask(offset, size)) >> shift;
}

static uint32_t imx95_fractpll_merge_subword(uint32_t old_value,
                                             hwaddr offset,
                                             uint64_t value,
                                             unsigned int size)
{
    unsigned int shift = (offset & 3U) * 8U;
    uint32_t mask = imx95_fractpll_access_mask(offset, size);

    return (old_value & ~mask) | (((uint32_t)value << shift) & mask);
}

static uint32_t imx95_fractpll_read_word(Imx95FractpllState *s,
                                         hwaddr word_offset)
{
    uint32_t value = s->regs[imx95_fractpll_reg_index(word_offset)];
    hwaddr block_offset = word_offset % IMX95_FRACTPLL_BLOCK_SIZE;

    if (block_offset == IMX95_FRACTPLL_STATUS_OFFSET) {
        value |= IMX95_FRACTPLL_STATUS_LOCK;
    } else if (block_offset == IMX95_FRACTPLL_DFS_STATUS_OFFSET) {
        value |= IMX95_FRACTPLL_DFS_ALL_OK;
    }

    return value;
}

static uint64_t imx95_fractpll_read(void *opaque, hwaddr offset,
                                    unsigned int size)
{
    Imx95FractpllState *s = opaque;
    hwaddr word_offset = offset & ~(hwaddr)3;
    uint32_t value;

    if (word_offset >= IMX95_FRACTPLL_SIZE) {
        return 0;
    }

    value = imx95_fractpll_read_word(s, word_offset);
    return imx95_fractpll_extract_subword(value, offset, size);
}

static void imx95_fractpll_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned int size)
{
    Imx95FractpllState *s = opaque;
    hwaddr word_offset = offset & ~(hwaddr)3;
    hwaddr block_offset;
    unsigned int alias;
    unsigned int register_index;
    unsigned int shift;
    uint32_t access_mask;
    uint32_t write_value;

    if (word_offset >= IMX95_FRACTPLL_SIZE) {
        return;
    }

    block_offset = word_offset % IMX95_FRACTPLL_BLOCK_SIZE;
    if (block_offset >= IMX95_FRACTPLL_STATUS_OFFSET) {
        register_index = imx95_fractpll_reg_index(word_offset);
        s->regs[register_index] = imx95_fractpll_merge_subword(
            s->regs[register_index], offset, value, size);
        return;
    }

    alias = (block_offset >> 2) & 0x3U;
    register_index = imx95_fractpll_reg_index(word_offset & ~(hwaddr)0xf);
    shift = (offset & 3U) * 8U;
    access_mask = imx95_fractpll_access_mask(offset, size);
    write_value = ((uint32_t)value << shift) & access_mask;

    switch (alias) {
    case 0: /* RW */
        s->regs[register_index] =
            (s->regs[register_index] & ~access_mask) | write_value;
        break;
    case 1: /* SET */
        s->regs[register_index] |= write_value;
        break;
    case 2: /* CLR */
        s->regs[register_index] &= ~write_value;
        break;
    case 3: /* TOG */
        s->regs[register_index] ^= write_value;
        break;
    default:
        g_assert_not_reached();
    }
}

static const MemoryRegionOps imx95_fractpll_ops = {
    .read = imx95_fractpll_read,
    .write = imx95_fractpll_write,
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

static void imx95_fractpll_reset(DeviceState *dev)
{
    Imx95FractpllState *s = IMX95_FRACTPLL(dev);

    memset(s->regs, 0, sizeof(s->regs));
    s->regs[imx95_fractpll_reg_index(
        IMX95_FRACTPLL_NUMERATOR_OFFSET)] =
        IMX95_SYSPLL1_NUMERATOR_RESET;
    s->regs[imx95_fractpll_reg_index(
        IMX95_FRACTPLL_DENOMINATOR_OFFSET)] =
        IMX95_SYSPLL1_DENOMINATOR_RESET;
    s->regs[imx95_fractpll_reg_index(IMX95_FRACTPLL_DIV_OFFSET)] =
        IMX95_SYSPLL1_DIV_RESET;
    s->regs[imx95_fractpll_reg_index(IMX95_FRACTPLL_DFS0_DIV_OFFSET)] =
        IMX95_SYSPLL1_DFS0_DIV_RESET;
    s->regs[imx95_fractpll_reg_index(IMX95_FRACTPLL_DFS1_DIV_OFFSET)] =
        IMX95_SYSPLL1_DFS1_DIV_RESET;
    s->regs[imx95_fractpll_reg_index(IMX95_FRACTPLL_DFS2_DIV_OFFSET)] =
        IMX95_SYSPLL1_DFS2_DIV_RESET;

    s->regs[imx95_fractpll_reg_index(IMX95_FRACTPLL_DRAM_PLL_OFFSET)] =
        IMX95_FRACTPLL_CTRL_POWERUP | IMX95_FRACTPLL_CTRL_CLKMUX_EN;
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
        VMSTATE_UINT32_ARRAY(regs, Imx95FractpllState,
                             IMX95_FRACTPLL_REG_COUNT),
        VMSTATE_END_OF_LIST()
    },
};

static void imx95_fractpll_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, imx95_fractpll_reset);
    dc->vmsd = &vmstate_imx95_fractpll;
    dc->desc = "i.MX95 fractional PLL register model";
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

