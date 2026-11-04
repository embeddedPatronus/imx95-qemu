/*
 * NXP i.MX95 blk ctrl s aonmix
 *
 * Secure AONMIX block control. Starts and stops the Cortex-A55 cores for
 * the System Manager: clearing a CPUWAIT bit starts the core at the reset
 * vector in CA55_RVBARADDR, setting it or asserting the core or platform
 * reset in the SRC stops the core. It also handles the cores' WFI power-
 * down and GIC wake-up requests and the ELE MU3 interrupt mask.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "system/memory.h"
#include "qemu/main-loop.h"
#include "target/arm/arm-powerctl.h"
#include "hw/core/cpu.h"
#include "hw/misc/imx95_blk_ctrl_s_aonmix.h"
#include "trace.h"

static bool imx95_blk_ctrl_a55_started(Imx95BlkCtrlSAonmixState *s,
                                       unsigned int cpu_index)
{
    return cpu_index == 0 ? s->a55_released
                          : s->a55_secondary_released[cpu_index];
}

static void imx95_blk_ctrl_a55_set_started(Imx95BlkCtrlSAonmixState *s,
                                           unsigned int cpu_index,
                                           bool started)
{
    if (cpu_index == 0) {
        s->a55_released = started;
    } else {
        s->a55_secondary_released[cpu_index] = started;
    }
}

static void imx95_blk_ctrl_a55_reconcile(Imx95A55Ctx *ctx);

/* Runs on the CPU thread, after the start/stop that was just requested. */
static void imx95_blk_ctrl_a55_op_done_work(CPUState *cs, run_on_cpu_data data)
{
    Imx95A55Ctx *ctx = data.host_ptr;

    ctx->s->a55_op_pending[ctx->idx] = false;
    imx95_blk_ctrl_a55_reconcile(ctx);
}

static void imx95_blk_ctrl_a55_op_issued(Imx95A55Ctx *ctx, CPUState *cs)
{
    ctx->s->a55_op_pending[ctx->idx] = true;
    async_run_on_cpu(cs, imx95_blk_ctrl_a55_op_done_work,
                     RUN_ON_CPU_HOST_PTR(ctx));
}

static void imx95_blk_ctrl_a55_reconcile(Imx95A55Ctx *ctx)
{
    Imx95BlkCtrlSAonmixState *s = ctx->s;
    unsigned int i = ctx->idx;
    uint64_t affinity = (uint64_t)i << 8;
    CPUState *cs = arm_get_cpu_by_id(affinity);
    int ret;

    if (cs == NULL || s->a55_op_pending[i]) {
        return;
    }

    if (!s->a55_want_on[i]) {
        if (arm_set_cpu_off(affinity) == QEMU_ARM_POWERCTL_RET_SUCCESS) {
            imx95_blk_ctrl_a55_op_issued(ctx, cs);
        }
        return;
    }

    ret = arm_set_cpu_on(affinity, s->a55_want_entry[i], 0, 3, true);
    if (ret == QEMU_ARM_POWERCTL_RET_SUCCESS) {
        s->a55_run_gen[i] = s->a55_want_gen[i];
        imx95_blk_ctrl_a55_set_started(s, i, true);
        trace_imx95_a55_started(i, s->a55_want_entry[i]);
        imx95_blk_ctrl_a55_op_issued(ctx, cs);
    } else if (ret == QEMU_ARM_POWERCTL_ALREADY_ON) {
        if (s->a55_run_gen[i] != s->a55_want_gen[i] &&
            arm_set_cpu_off(affinity) == QEMU_ARM_POWERCTL_RET_SUCCESS) {
            /* Running for an older request: stop it, then start again. */
            imx95_blk_ctrl_a55_op_issued(ctx, cs);
        }
    } else {
        error_report("imx95-a55: CPU%u arm_set_cpu_on failed, "
                     "ret=%d entry=0x%016" PRIx64,
                     i, ret, s->a55_want_entry[i]);
    }
}

static void imx95_blk_ctrl_a55_reconcile_work(CPUState *cs,
                                              run_on_cpu_data data)
{
    imx95_blk_ctrl_a55_reconcile(data.host_ptr);
}

static void imx95_blk_ctrl_a55_kick(Imx95BlkCtrlSAonmixState *s,
                                    unsigned int cpu_index)
{
    CPUState *cs = arm_get_cpu_by_id((uint64_t)cpu_index << 8);

    if (cs != NULL) {
        async_run_on_cpu(cs, imx95_blk_ctrl_a55_reconcile_work,
                         RUN_ON_CPU_HOST_PTR(&s->a55_ctx[cpu_index]));
    }
}

static void imx95_blk_ctrl_a55_start(Imx95BlkCtrlSAonmixState *s,
                                     unsigned int cpu_index, uint64_t entry)
{
    s->a55_want_on[cpu_index] = true;
    s->a55_want_entry[cpu_index] = entry;
    s->a55_want_gen[cpu_index]++;
    imx95_blk_ctrl_a55_kick(s, cpu_index);
}

static void imx95_blk_ctrl_a55_stop(Imx95BlkCtrlSAonmixState *s,
                                    unsigned int cpu_index)
{
    s->a55_want_on[cpu_index] = false;
    s->a55_pwrdn[cpu_index] = false;
    imx95_blk_ctrl_a55_set_started(s, cpu_index, false);
    imx95_blk_ctrl_a55_kick(s, cpu_index);
}

static uint64_t imx95_blk_ctrl_a55_rvbar(Imx95BlkCtrlSAonmixState *s,
                                         unsigned int cpu_index)
{
    hwaddr l = BLK_CTRL_CA55_RVBARADDR0_L + cpu_index * 8;
    hwaddr h = BLK_CTRL_CA55_RVBARADDR0_H + cpu_index * 8;

    return ((((uint64_t)s->regs[h / 4] & 0x3fU) << 32) | s->regs[l / 4]) << 2;
}

static void imx95_blk_ctrl_a55_wake_eval(Imx95BlkCtrlSAonmixState *s,
                                         unsigned int cpu_index)
{
    uint64_t entry;

    if (!s->a55_pwrdn[cpu_index] || !s->a55_gic_wake[cpu_index] ||
        !imx95_blk_ctrl_a55_started(s, cpu_index) ||
        s->a55_op_pending[cpu_index]) {
        return;
    }

    entry = imx95_blk_ctrl_a55_rvbar(s, cpu_index);
    if (arm_set_cpu_on((uint64_t)cpu_index << 8, entry, 0, 3, true) == 0) {
        s->a55_pwrdn[cpu_index] = false;
    }
}

static void imx95_blk_ctrl_a55_pwrdn_in(void *opaque, int n, int level)
{
    Imx95BlkCtrlSAonmixState *s = opaque;

    s->a55_pwrdn[n] = level;
    imx95_blk_ctrl_a55_wake_eval(s, n);
}

static void imx95_blk_ctrl_a55_gic_wake_in(void *opaque, int n, int level)
{
    Imx95BlkCtrlSAonmixState *s = opaque;

    s->a55_gic_wake[n] = level;
    imx95_blk_ctrl_a55_wake_eval(s, n);
}

static uint64_t imx95_blk_ctrl_s_aonmix_read(void *opaque,
                                             hwaddr offset,
                                             unsigned size)
{
    Imx95BlkCtrlSAonmixState *s = opaque;
    uint32_t value;

    if (offset >= IMX95_BLK_CTRL_S_AONMIX_SIZE) {
        return 0;
    }

    value = s->regs[offset / 4];

    return value;
}

/*
 * Reset of a Cortex-A55 core (n < 6) or of the A55 platform (n == 6) asserted
 * in the SRC. The platform reset takes every core with it (RM 19.3.11.3).
 */
static void imx95_blk_ctrl_a55_hold_in(void *opaque, int n, int level)
{
    Imx95BlkCtrlSAonmixState *s = opaque;

    if (!level) {
        return;
    }
    if (n < IMX95_BLK_CTRL_NUM_A55) {
        imx95_blk_ctrl_a55_stop(s, n);
    } else {
        for (unsigned int i = 0; i < IMX95_BLK_CTRL_NUM_A55; i++) {
            imx95_blk_ctrl_a55_stop(s, i);
        }
    }
}

static void imx95_blk_ctrl_s_aonmix_check_a55_release(
    Imx95BlkCtrlSAonmixState *s,
    uint32_t old_wait,
    uint32_t new_wait)
{
    unsigned int cpu_index;

    for (cpu_index = 0;
         cpu_index < IMX95_BLK_CTRL_NUM_A55;
         cpu_index++) {
        hwaddr rvbar_l_offset;
        hwaddr rvbar_h_offset;
        uint64_t rvbar_encoded;
        uint64_t reset_vector;

        /* CPUWAIT zero-to-one: the core is held (stopped) again. */
        if ((old_wait & BIT(cpu_index)) == 0 &&
            (new_wait & BIT(cpu_index)) != 0) {
            imx95_blk_ctrl_a55_stop(s, cpu_index);
            continue;
        }

        /*
         * A CPU is released when its CPUWAIT bit makes a one-to-zero
         * transition. Ignore bits that were already clear and bits that
         * remain asserted.
         */
        if ((old_wait & BIT(cpu_index)) == 0 ||
            (new_wait & BIT(cpu_index)) != 0) {
            continue;
        }

        /*
         * Each CPU has an adjacent RVBAR low/high register pair.
         * CPU0 starts at 0x164/0x168 and each following pair is eight
         * bytes later.
         */
        rvbar_l_offset =
           BLK_CTRL_CA55_RVBARADDR0_L + cpu_index * 8;
        rvbar_h_offset =
            BLK_CTRL_CA55_RVBARADDR0_H + cpu_index * 8;

        rvbar_encoded =
            (((uint64_t)s->regs[rvbar_h_offset / sizeof(uint32_t)] &
              0x3fU) << 32) |
            s->regs[rvbar_l_offset / sizeof(uint32_t)];
        reset_vector = rvbar_encoded << 2;

        trace_imx95_a55_release_request(cpu_index, rvbar_encoded,
                                        reset_vector, old_wait, new_wait);

        if (reset_vector == 0) {
            trace_imx95_a55_release_ignored(cpu_index);
            continue;
        }

        if (cpu_index == 0) {
            if (!s->a55_spl_loaded) {
                error_report("imx95-a55: CPU0 release requested but "
                             "A55 SPL is not loaded");
                continue;
            }
            if (reset_vector != s->a55_spl_entry) {
                trace_imx95_a55_entry_differs(reset_vector,
                                              (uint64_t)s->a55_spl_entry);
            }
        }

        imx95_blk_ctrl_a55_start(s, cpu_index, reset_vector);
    }
}

static void imx95_blk_ctrl_s_aonmix_write(void *opaque,
                                          hwaddr offset,
                                          uint64_t value,
                                          unsigned size)
{
    Imx95BlkCtrlSAonmixState *s = opaque;
    uint32_t old_value;

    if (offset >= IMX95_BLK_CTRL_S_AONMIX_SIZE) {
        return;
    }

    old_value = s->regs[offset / 4];
    s->regs[offset / 4] = (uint32_t)value;

    if (offset == IMX95_ELE_MU3_CA55_MASK_REG) {
        qemu_set_irq(s->ele_mu3_irq_mask,
                     ((uint32_t)value & IMX95_ELE_MU3_CA55_MASK_BIT) != 0U);
        return;
    }
    if (offset == BLK_CTRL_CA55_CPUWAIT) {
        imx95_blk_ctrl_s_aonmix_check_a55_release(s, old_value,
                                                  (uint32_t)value);
    }
}

static const MemoryRegionOps imx95_blk_ctrl_s_aonmix_ops = {
    .read = imx95_blk_ctrl_s_aonmix_read,
    .write = imx95_blk_ctrl_s_aonmix_write,
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


static void imx95_blk_ctrl_s_aonmix_reset(DeviceState *dev)
{
    Imx95BlkCtrlSAonmixState *s = IMX95_BLK_CTRL_S_AONMIX(dev);

    memset(s->regs, 0, sizeof(s->regs));

    s->regs[BLK_CTRL_CA55_CPUWAIT / 4] = 0x3f;

    for (unsigned int offset = 0x40; offset <= 0x6c; offset += 4) {
        s->regs[offset / sizeof(uint32_t)] = UINT32_MAX;
    }

    s->regs[IMX95_ELE_MU3_CA55_MASK_REG / sizeof(uint32_t)] &=
        ~IMX95_ELE_MU3_CA55_MASK_BIT;

    s->a55_released = false;
    memset(s->a55_pwrdn, 0, sizeof(s->a55_pwrdn));
    memset(s->a55_gic_wake, 0, sizeof(s->a55_gic_wake));
    memset(s->a55_secondary_released, 0, sizeof(s->a55_secondary_released));
    memset(s->a55_want_on, 0, sizeof(s->a55_want_on));
    memset(s->a55_want_entry, 0, sizeof(s->a55_want_entry));
    memset(s->a55_want_gen, 0, sizeof(s->a55_want_gen));
    memset(s->a55_run_gen, 0, sizeof(s->a55_run_gen));
    memset(s->a55_op_pending, 0, sizeof(s->a55_op_pending));

    qemu_set_irq(s->ele_mu3_irq_mask,
                 (s->regs[IMX95_ELE_MU3_CA55_MASK_REG / sizeof(uint32_t)] &
                  IMX95_ELE_MU3_CA55_MASK_BIT) != 0U);
}

static void imx95_blk_ctrl_s_aonmix_init(Object *obj)
{
    Imx95BlkCtrlSAonmixState *s = IMX95_BLK_CTRL_S_AONMIX(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_blk_ctrl_s_aonmix_ops, s,
                          TYPE_IMX95_BLK_CTRL_S_AONMIX, IMX95_BLK_CTRL_S_AONMIX_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    qdev_init_gpio_out_named(DEVICE(obj), &s->ele_mu3_irq_mask, "ele-mu3-irq-mask", 1);
    qdev_init_gpio_in_named(DEVICE(obj), imx95_blk_ctrl_a55_pwrdn_in,
                            "a55-wfi-powerdown", IMX95_BLK_CTRL_NUM_A55);
    qdev_init_gpio_in_named(DEVICE(obj), imx95_blk_ctrl_a55_gic_wake_in,
                            "gic-wakeup-req", IMX95_BLK_CTRL_NUM_A55);
    /* SRC: reset of core 0-5 asserted, and of the A55 platform (line 6) */
    qdev_init_gpio_in_named(DEVICE(obj), imx95_blk_ctrl_a55_hold_in,
                            "a55-hold", IMX95_BLK_CTRL_NUM_A55 + 1);
    for (unsigned int i = 0; i < IMX95_BLK_CTRL_NUM_A55; i++) {
        s->a55_ctx[i].s = s;
        s->a55_ctx[i].idx = i;
    }
}

static const VMStateDescription vmstate_imx95_blk_ctrl_s_aonmix = {
    .name = TYPE_IMX95_BLK_CTRL_S_AONMIX,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, Imx95BlkCtrlSAonmixState, IMX95_BLK_CTRL_S_AONMIX_SIZE / 4),
        VMSTATE_BOOL(a55_released, Imx95BlkCtrlSAonmixState),
        VMSTATE_BOOL_ARRAY(a55_secondary_released, Imx95BlkCtrlSAonmixState, IMX95_BLK_CTRL_NUM_A55),
        VMSTATE_END_OF_LIST()
    }
};

static const Property imx95_blk_ctrl_s_aonmix_properties[] = {
    DEFINE_PROP_BOOL("a55-spl-loaded", Imx95BlkCtrlSAonmixState,
                     a55_spl_loaded, false),
    DEFINE_PROP_UINT64("a55-spl-entry", Imx95BlkCtrlSAonmixState,
                       a55_spl_entry, 0),
};

static void imx95_blk_ctrl_s_aonmix_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, imx95_blk_ctrl_s_aonmix_reset);
    dc->vmsd = &vmstate_imx95_blk_ctrl_s_aonmix;
    dc->desc = "i.MX95 blk ctrl s aonmix";
    device_class_set_props(dc, imx95_blk_ctrl_s_aonmix_properties);
}

static const TypeInfo imx95_blk_ctrl_s_aonmix_types[] = {
    {
        .name = TYPE_IMX95_BLK_CTRL_S_AONMIX,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Imx95BlkCtrlSAonmixState),
        .instance_init = imx95_blk_ctrl_s_aonmix_init,
        .class_init = imx95_blk_ctrl_s_aonmix_class_init,
    },
};

DEFINE_TYPES(imx95_blk_ctrl_s_aonmix_types)
