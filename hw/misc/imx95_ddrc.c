/*
 * NXP i.MX95 DDR controller
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qapi/error.h"
#include "hw/core/qdev-properties.h"
#include "hw/misc/imx95_ddrc.h"
#include "migration/vmstate.h"

#define DDRC_CS0_BNDS        0x000
#define DDRC_CS1_BNDS        0x008
#define DDRC_CS0_CONFIG      0x080
#define DDRC_CS1_CONFIG      0x084
#define DDRC_DDR_SDRAM_CFG   0x110
#define DDRC_DDRDSR_2        0xb24

#define DDRC_CS_CONFIG_CS_EN          BIT(31)
#define DDRC_CS_CONFIG_AP_EN          BIT(23)
#define DDRC_CS_CONFIG_ROW_BITS_SHIFT 8
#define DDRC_CS_CONFIG_BG_BITS_SHIFT  4
#define DDRC_CS_CONFIG_COL_BITS_SHIFT 0
#define DDRC_CS_CONFIG_ROW_BITS_MASK  0x00000700U
#define DDRC_CS_CONFIG_BG_BITS_MASK   0x00000030U
#define DDRC_CS_CONFIG_COL_BITS_MASK  0x00000007U
#define DDRC_CS_CONFIG_WRITABLE_MASK  \
    (DDRC_CS_CONFIG_CS_EN | DDRC_CS_CONFIG_AP_EN | \
     DDRC_CS_CONFIG_ROW_BITS_MASK | DDRC_CS_CONFIG_BG_BITS_MASK | \
     DDRC_CS_CONFIG_COL_BITS_MASK)

#define DDRC_BNDS_ADDR_MASK   0x0fffU
#define DDRC_BNDS_SA_SHIFT    16
#define DDRC_BNDS_UNIT_SHIFT  24
#define DDRC_BNDS_UNIT_SIZE   (1ULL << DDRC_BNDS_UNIT_SHIFT)
#define DDRC_ADDRESS_LIMIT    (1ULL << 36)
#define DDRC_BNDS_WRITABLE_MASK \
    ((DDRC_BNDS_ADDR_MASK << DDRC_BNDS_SA_SHIFT) | DDRC_BNDS_ADDR_MASK)

#define DDRC_DDR_SDRAM_CFG_MEM_EN BIT(31)
#define DDRC_DDR_SDRAM_CFG_RESET  0x07000000U

#define DDRC_DDRDSR_2_IDLE             BIT(31)
#define DDRC_DDRDSR_2_NML              BIT(30)
#define DDRC_DDRDSR_2_PHY_INIT_COMPLETE BIT(2)
#define DDRC_DDRDSR_2_RESET \
    (DDRC_DDRDSR_2_IDLE | DDRC_DDRDSR_2_NML)

static unsigned int imx95_ddrc_reg_index(hwaddr offset)
{
    return offset / sizeof(uint32_t);
}

static uint32_t imx95_ddrc_encode_bounds(uint64_t base, uint64_t size)
{
    uint64_t start = base >> DDRC_BNDS_UNIT_SHIFT;
    uint64_t end = (base + size - 1) >> DDRC_BNDS_UNIT_SHIFT;

    return ((start & DDRC_BNDS_ADDR_MASK) << DDRC_BNDS_SA_SHIFT) |
           (end & DDRC_BNDS_ADDR_MASK);
}

static int imx95_ddrc_encode_row_bits(unsigned int bits)
{
    return bits >= 12 && bits <= 18 ? bits - 12 : -1;
}

static int imx95_ddrc_encode_column_bits(unsigned int bits)
{
    if (bits == 7) {
        return 7;
    }
    return bits >= 8 && bits <= 11 ? bits - 8 : -1;
}

static uint32_t imx95_ddrc_encode_rank_config(Imx95DdrcState *s)
{
    int row = imx95_ddrc_encode_row_bits(s->row_bits);
    int column = imx95_ddrc_encode_column_bits(s->column_bits);

    return DDRC_CS_CONFIG_CS_EN |
           ((uint32_t)row << DDRC_CS_CONFIG_ROW_BITS_SHIFT) |
           ((uint32_t)s->bank_group_bits <<
            DDRC_CS_CONFIG_BG_BITS_SHIFT) |
           ((uint32_t)column << DDRC_CS_CONFIG_COL_BITS_SHIFT);
}

static void imx95_ddrc_update_state(Imx95DdrcState *s)
{
    uint32_t cs0 = s->regs[imx95_ddrc_reg_index(DDRC_CS0_CONFIG)];
    uint32_t cs1 = s->regs[imx95_ddrc_reg_index(DDRC_CS1_CONFIG)];
    uint32_t *status = &s->regs[imx95_ddrc_reg_index(DDRC_DDRDSR_2)];

    if ((cs0 | cs1) & DDRC_CS_CONFIG_CS_EN) {
        s->phy_init_complete = true;
        *status |= DDRC_DDRDSR_2_PHY_INIT_COMPLETE;
    }

    s->memory_enabled =
        s->regs[imx95_ddrc_reg_index(DDRC_DDR_SDRAM_CFG)] &
        DDRC_DDR_SDRAM_CFG_MEM_EN;
}

static uint64_t imx95_ddrc_read(void *opaque, hwaddr offset,
                                unsigned int size)
{
    Imx95DdrcState *s = opaque;

    if (offset >= IMX95_DDRC_SIZE) {
        return 0;
    }

    return s->regs[imx95_ddrc_reg_index(offset)];
}

static void imx95_ddrc_write(void *opaque, hwaddr offset,
                             uint64_t value, unsigned int size)
{
    Imx95DdrcState *s = opaque;
    unsigned int index;
    uint32_t writable_mask = UINT32_MAX;

    if (offset >= IMX95_DDRC_SIZE) {
        return;
    }

    index = imx95_ddrc_reg_index(offset);

    switch (offset) {
    case DDRC_CS0_BNDS:
    case DDRC_CS1_BNDS:
        writable_mask = DDRC_BNDS_WRITABLE_MASK;
        break;
    case DDRC_CS0_CONFIG:
    case DDRC_CS1_CONFIG:
        writable_mask = DDRC_CS_CONFIG_WRITABLE_MASK;
        break;
    case DDRC_DDRDSR_2:
        return;
    default:
        break;
    }

    s->regs[index] = (s->regs[index] & ~writable_mask) |
                     ((uint32_t)value & writable_mask);
    imx95_ddrc_update_state(s);
}

static const MemoryRegionOps imx95_ddrc_ops = {
    .read = imx95_ddrc_read,
    .write = imx95_ddrc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static bool imx95_ddrc_validate_geometry(Imx95DdrcState *s, Error **errp)
{
    uint64_t rank0_size;
    uint64_t rank1_size;
    uint64_t end;

    if (!s->dram_size) {
        error_setg(errp, "dram-size must be nonzero");
        return false;
    }
    if (!QEMU_IS_ALIGNED(s->dram_base, DDRC_BNDS_UNIT_SIZE)) {
        error_setg(errp, "dram-base must be 16 MiB aligned");
        return false;
    }
    if (!QEMU_IS_ALIGNED(s->dram_size, DDRC_BNDS_UNIT_SIZE)) {
        error_setg(errp, "dram-size must be a multiple of 16 MiB");
        return false;
    }
    if (s->dram_base > DDRC_ADDRESS_LIMIT - s->dram_size) {
        error_setg(errp, "DRAM range exceeds the 36-bit DDRC address space");
        return false;
    }
    if (s->rank_count < 1 || s->rank_count > IMX95_DDRC_NUM_RANKS) {
        error_setg(errp, "rank-count must be 1 or 2");
        return false;
    }
    if (imx95_ddrc_encode_row_bits(s->row_bits) < 0) {
        error_setg(errp, "row-bits must be between 12 and 18");
        return false;
    }
    if (imx95_ddrc_encode_column_bits(s->column_bits) < 0) {
        error_setg(errp, "column-bits must be 7, 8, 9, 10 or 11");
        return false;
    }
    if (s->bank_group_bits > 2) {
        error_setg(errp, "bank-group-bits must be between 0 and 2");
        return false;
    }

    rank0_size = s->rank0_size;
    if (!rank0_size) {
        rank0_size = s->rank_count == 1 ? s->dram_size : s->dram_size / 2;
    }
    rank1_size = s->dram_size - rank0_size;

    if (!rank0_size || !QEMU_IS_ALIGNED(rank0_size, DDRC_BNDS_UNIT_SIZE)) {
        error_setg(errp, "rank 0 size must be a nonzero multiple of 16 MiB");
        return false;
    }
    if (s->rank_count == 1 && rank1_size) {
        error_setg(errp, "rank 0 size must equal dram-size for one rank");
        return false;
    }
    if (s->rank_count == 2 &&
        (!rank1_size || !QEMU_IS_ALIGNED(rank1_size,
                                         DDRC_BNDS_UNIT_SIZE))) {
        error_setg(errp, "rank 1 size must be a nonzero multiple of 16 MiB");
        return false;
    }

    end = s->dram_base + s->dram_size;
    if ((s->dram_base >> DDRC_BNDS_UNIT_SHIFT) > DDRC_BNDS_ADDR_MASK ||
        ((end - 1) >> DDRC_BNDS_UNIT_SHIFT) > DDRC_BNDS_ADDR_MASK) {
        error_setg(errp, "DRAM range cannot be encoded in CSn_BNDS");
        return false;
    }

    s->rank0_size = rank0_size;
    return true;
}

static void imx95_ddrc_reset(DeviceState *dev)
{
    Imx95DdrcState *s = IMX95_DDRC(dev);
    uint64_t rank1_base = s->dram_base + s->rank0_size;
    uint64_t rank1_size = s->dram_size - s->rank0_size;
    uint32_t rank_config = imx95_ddrc_encode_rank_config(s);

    memset(s->regs, 0, sizeof(s->regs));

    // OEI programming is preloaded because OEI execution is not modeled.
    s->regs[imx95_ddrc_reg_index(DDRC_CS0_BNDS)] =
        imx95_ddrc_encode_bounds(s->dram_base, s->rank0_size);
    s->regs[imx95_ddrc_reg_index(DDRC_CS0_CONFIG)] = rank_config;

    if (s->rank_count == 2) {
        s->regs[imx95_ddrc_reg_index(DDRC_CS1_BNDS)] =
            imx95_ddrc_encode_bounds(rank1_base, rank1_size);
        s->regs[imx95_ddrc_reg_index(DDRC_CS1_CONFIG)] = rank_config;
    }

    s->regs[imx95_ddrc_reg_index(DDRC_DDR_SDRAM_CFG)] =
        DDRC_DDR_SDRAM_CFG_RESET;
    s->regs[imx95_ddrc_reg_index(DDRC_DDRDSR_2)] = DDRC_DDRDSR_2_RESET;
    s->phy_init_complete = false;
    s->memory_enabled = false;
    imx95_ddrc_update_state(s);
}

static void imx95_ddrc_realize(DeviceState *dev, Error **errp)
{
    Imx95DdrcState *s = IMX95_DDRC(dev);

    if (!imx95_ddrc_validate_geometry(s, errp)) {
        return;
    }
}

static void imx95_ddrc_init(Object *obj)
{
    Imx95DdrcState *s = IMX95_DDRC(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_ddrc_ops, s,
                          TYPE_IMX95_DDRC, IMX95_DDRC_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static int imx95_ddrc_post_load(void *opaque, int version_id)
{
    Imx95DdrcState *s = opaque;

    s->phy_init_complete =
        s->regs[imx95_ddrc_reg_index(DDRC_DDRDSR_2)] &
        DDRC_DDRDSR_2_PHY_INIT_COMPLETE;
    s->memory_enabled =
        s->regs[imx95_ddrc_reg_index(DDRC_DDR_SDRAM_CFG)] &
        DDRC_DDR_SDRAM_CFG_MEM_EN;

    return 0;
}

static const Property imx95_ddrc_properties[] = {
    DEFINE_PROP_UINT64("dram-base", Imx95DdrcState, dram_base, 0),
    DEFINE_PROP_UINT64("dram-size", Imx95DdrcState, dram_size, 0),
    DEFINE_PROP_UINT64("rank0-size", Imx95DdrcState, rank0_size, 0),
    DEFINE_PROP_UINT8("rank-count", Imx95DdrcState, rank_count, 1),
    DEFINE_PROP_UINT8("row-bits", Imx95DdrcState, row_bits, 18),
    DEFINE_PROP_UINT8("column-bits", Imx95DdrcState, column_bits, 10),
    DEFINE_PROP_UINT8("bank-group-bits", Imx95DdrcState,
                      bank_group_bits, 2),
};

static const VMStateDescription vmstate_imx95_ddrc = {
    .name = TYPE_IMX95_DDRC,
    .version_id = 2,
    .minimum_version_id = 2,
    .post_load = imx95_ddrc_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, Imx95DdrcState, IMX95_DDRC_REG_COUNT),
        VMSTATE_END_OF_LIST()
    },
};

static void imx95_ddrc_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = imx95_ddrc_realize;
    device_class_set_legacy_reset(dc, imx95_ddrc_reset);
    device_class_set_props(dc, imx95_ddrc_properties);
    dc->vmsd = &vmstate_imx95_ddrc;
    dc->desc = "i.MX95 DDR controller";
}

static const TypeInfo imx95_ddrc_types[] = {
    {
        .name = TYPE_IMX95_DDRC,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Imx95DdrcState),
        .instance_init = imx95_ddrc_init,
        .class_init = imx95_ddrc_class_init,
    },
};

DEFINE_TYPES(imx95_ddrc_types)

