/*
 * NXP i.MX95 src_gen
 *
 * System Reset Controller. Each slice has its software control register
 * and the FSM_STAT, FUNC_STAT and RSTR_STAT status registers. Power-up and
 * power-down complete immediately, and asserting the reset of a Cortex-A55
 * core or of the A55 platform signals the block control to stop the core.
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
#include "hw/misc/imx95_src_gen.h"
#include "migration/vmstate.h"
#include "hw/core/irq.h"
#include "system/memory.h"
#include "trace.h"

#define IMX95_SRC_GEN_SLICE_SIZE         0x400
#define IMX95_SRC_GEN_FSM_STAT_OFFSET    0x0b0
#define IMX95_SRC_GEN_RSTR_STAT_OFFSET   0x0b8
#define IMX95_SRC_GEN_RST_RSTR_SHIFT     20
#define IMX95_SRC_GEN_RST_RSTR_MASK      0xfU
#define IMX95_SRC_GEN_A55_CORE0_SLICE    0x1800
#define IMX95_SRC_GEN_A55_PLATFORM_SLICE 0x3000
#define IMX95_SRC_GEN_FUNC_STAT_OFFSET   0x0b4
#define IMX95_SRC_GEN_FSM_STAT_RST_DONE  (0x9U << 4)
#define IMX95_SRC_GEN_SLICE_SW_CTRL_OFFSET    0x020
#define IMX95_SRC_GEN_SLICE_PDN_SOFT          BIT(31)
#define IMX95_SRC_GEN_FUNC_STAT_PUP           0x00000004U
#define IMX95_SRC_GEN_FUNC_STAT_PDN           0x00005511U


static bool imx95_src_gen_is_slice_reg(hwaddr offset,
                                      hwaddr register_offset)
{
    return offset >= IMX95_SRC_GEN_SLICE_SIZE &&
           (offset % IMX95_SRC_GEN_SLICE_SIZE) == register_offset;
}

static uint64_t imx95_src_gen_read(void *opaque, hwaddr offset,
                                   unsigned size)
{
    Imx95SrcGenState *s = opaque;
    hwaddr word_offset = offset & ~3ULL;
    hwaddr byte_offset = offset & 3ULL;
    uint32_t value;

    if (word_offset >= IMX95_SRC_GEN_SIZE) {
        return 0;
    }

    value = s->regs[word_offset / sizeof(uint32_t)];

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

static void imx95_src_gen_write(void *opaque, hwaddr offset,
                                uint64_t value, unsigned size)
{
    Imx95SrcGenState *s = opaque;
    hwaddr word_offset = offset & ~3ULL;
    hwaddr byte_offset = offset & 3ULL;
    hwaddr slice_base;
    hwaddr func_stat_offset;
    uint32_t old_value;
    uint32_t new_value;
    uint32_t shift;
    uint32_t mask;

    if (word_offset >= IMX95_SRC_GEN_SIZE) {
        return;
    }

    old_value = s->regs[word_offset / sizeof(uint32_t)];

    if (size == 4) {
        s->regs[word_offset / sizeof(uint32_t)] = value;
        new_value = (uint32_t)value;
    } else {
        shift = byte_offset * 8;
        mask = (size == 1 ? UINT8_MAX : UINT16_MAX) << shift;
        new_value = (old_value & ~mask) |
                    (((uint32_t)value << shift) & mask);
    }

    s->regs[word_offset / sizeof(uint32_t)] = new_value;

    if (!imx95_src_gen_is_slice_reg(
            word_offset, IMX95_SRC_GEN_SLICE_SW_CTRL_OFFSET)) {
        return;
    }

    slice_base = word_offset -
                 IMX95_SRC_GEN_SLICE_SW_CTRL_OFFSET;
    func_stat_offset = slice_base +
                       IMX95_SRC_GEN_FUNC_STAT_OFFSET;

    {
        int line = -1;

        if (slice_base == IMX95_SRC_GEN_A55_PLATFORM_SLICE) {
            line = 6;
        } else if (slice_base >= IMX95_SRC_GEN_A55_CORE0_SLICE &&
                   slice_base <= IMX95_SRC_GEN_A55_CORE0_SLICE + 5 * IMX95_SRC_GEN_SLICE_SIZE) {
            line = (slice_base - IMX95_SRC_GEN_A55_CORE0_SLICE) /
                   IMX95_SRC_GEN_SLICE_SIZE;
        }
        if (line >= 0) {
            bool hold = (new_value &
                         BIT(IMX95_SRC_GEN_RST_RSTR_SHIFT)) != 0;

            if (hold != s->a55_hold_state[line]) {
                s->a55_hold_state[line] = hold;
                qemu_set_irq(s->a55_hold[line], hold);
            }
        }
    }

    if (slice_base + IMX95_SRC_GEN_RSTR_STAT_OFFSET < IMX95_SRC_GEN_SIZE) {
        s->regs[(slice_base + IMX95_SRC_GEN_RSTR_STAT_OFFSET) /
                sizeof(uint32_t)] =
            (new_value >> IMX95_SRC_GEN_RST_RSTR_SHIFT) &
            IMX95_SRC_GEN_RST_RSTR_MASK;
    }

    if (func_stat_offset >= IMX95_SRC_GEN_SIZE) {
        return;
    }

    if ((new_value & IMX95_SRC_GEN_SLICE_PDN_SOFT) != 0U) {
        s->regs[func_stat_offset / sizeof(uint32_t)] =
            IMX95_SRC_GEN_FUNC_STAT_PDN;
    } else {
        s->regs[func_stat_offset / sizeof(uint32_t)] =
            IMX95_SRC_GEN_FUNC_STAT_PUP;
    }

    if ((old_value ^ new_value) & IMX95_SRC_GEN_SLICE_PDN_SOFT) {
        trace_imx95_src_gen_power(slice_base, new_value,
                                  s->regs[func_stat_offset /
                                          sizeof(uint32_t)]);
    }

}

static const MemoryRegionOps imx95_src_gen_ops = {
    .read = imx95_src_gen_read,
    .write = imx95_src_gen_write,
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

static void imx95_src_gen_reset(DeviceState *dev)
{
    Imx95SrcGenState *s = IMX95_SRC_GEN(dev);
    memset(s->regs, 0, sizeof(s->regs));
    memset(s->a55_hold_state, 0, sizeof(s->a55_hold_state));

    hwaddr slice_base;

    for (slice_base = IMX95_SRC_GEN_SLICE_SIZE;
         slice_base + IMX95_SRC_GEN_FUNC_STAT_OFFSET <
             IMX95_SRC_GEN_SIZE;
         slice_base += IMX95_SRC_GEN_SLICE_SIZE) {
        s->regs[(slice_base + IMX95_SRC_GEN_FUNC_STAT_OFFSET) / sizeof(uint32_t)] = IMX95_SRC_GEN_FUNC_STAT_PUP;
        s->regs[(slice_base + IMX95_SRC_GEN_FSM_STAT_OFFSET) / sizeof(uint32_t)] =
            IMX95_SRC_GEN_FSM_STAT_RST_DONE;
    }
}

static void imx95_src_gen_init(Object *obj)
{
    Imx95SrcGenState *s = IMX95_SRC_GEN(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_src_gen_ops, s,
                          TYPE_IMX95_SRC_GEN, IMX95_SRC_GEN_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    qdev_init_gpio_out_named(DEVICE(obj), s->a55_hold, "a55-hold", 7);
}

static const VMStateDescription vmstate_imx95_src_gen = {
    .name = TYPE_IMX95_SRC_GEN,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, Imx95SrcGenState, IMX95_SRC_GEN_SIZE / sizeof(uint32_t)),
        VMSTATE_END_OF_LIST()
    }
};

static void imx95_src_gen_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, imx95_src_gen_reset);
    dc->vmsd = &vmstate_imx95_src_gen;
    dc->desc = "i.MX95 src gen";
}

static const TypeInfo imx95_src_gen_types[] = {
    {
        .name = TYPE_IMX95_SRC_GEN,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Imx95SrcGenState),
        .instance_init = imx95_src_gen_init,
        .class_init = imx95_src_gen_class_init,
    },
};

DEFINE_TYPES(imx95_src_gen_types)
