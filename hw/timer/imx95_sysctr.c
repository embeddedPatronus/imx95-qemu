/*
 * NXP i.MX95 System Counter (SYS_CTR)
 *
 * The counter counts at the 24 MHz base frequency while CNTCR.EN is set. When
 * the alternate frequency is selected (CNTCR.FCR1) the hardware adds
 * base/alternate per tick instead, so the count value advances at the same
 * rate; only the frequency status changes.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "qemu/host-utils.h"
#include "hw/core/irq.h"
#include "migration/vmstate.h"
#include "system/memory.h"
#include "hw/timer/imx95_sysctr.h"

#define SYSCTR_BASE_HZ          24000000U
#define SYSCTR_ALT_HZ           512U
#define SYSCTR_CNT_MASK         MAKE_64BIT_MASK(0, 56)

/* control frame */
#define CNTCR                   0x000
#define   CNTCR_EN              BIT(0)
#define   CNTCR_HDBG            BIT(1)
#define   CNTCR_FCR0            BIT(8)
#define   CNTCR_FCR1            BIT(9)
#define CNTSR                   0x004
#define   CNTSR_FCA0            BIT(8)
#define   CNTSR_FCA1            BIT(9)
#define CNTCV0                  0x008
#define CNTCV1                  0x00c
#define CNTFID0                 0x020
#define CNTFID1                 0x024
#define CNTFID2                 0x028
#define CNTID0                  0xfd0

/* compare frame n lives at 0x20 + 0x100 * n */
#define CMP_FRAME_STRIDE        0x100
#define CMP_FRAME_BASE          0x020
#define CMPCVL                  0x00
#define CMPCVH                  0x04
#define CMPCR                   0x0c
#define   CMPCR_EN              BIT(0)
#define   CMPCR_IMASK           BIT(1)
#define   CMPCR_ISTAT           BIT(2)

static uint64_t sysctr_count(Imx95SysctrState *s)
{
    int64_t dt;

    if (!(s->cntcr & CNTCR_EN)) {
        return s->cnt_base;
    }
    dt = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - s->t_base;
    if (dt < 0) {
        dt = 0;
    }
    return (s->cnt_base + muldiv64(dt, SYSCTR_BASE_HZ,
                                   NANOSECONDS_PER_SECOND)) & SYSCTR_CNT_MASK;
}

static bool sysctr_istat(Imx95SysctrState *s, unsigned f, uint64_t cnt)
{
    return (s->cmpcr[f] & CMPCR_EN) && cnt >= s->cmpcv[f];
}

static void sysctr_update(Imx95SysctrState *s)
{
    uint64_t cnt = sysctr_count(s);
    int64_t next = INT64_MAX;

    for (unsigned f = 0; f < IMX95_SYSCTR_NUM_COMPARE; f++) {
        bool istat = sysctr_istat(s, f, cnt);

        qemu_set_irq(s->irq[f], istat && !(s->cmpcr[f] & CMPCR_IMASK));

        if ((s->cmpcr[f] & CMPCR_EN) && !istat && (s->cntcr & CNTCR_EN)) {
            uint64_t diff = s->cmpcv[f] - s->cnt_base;
            uint64_t ns = muldiv64(diff, NANOSECONDS_PER_SECOND,
                                   SYSCTR_BASE_HZ) + 1;

            if (ns < (uint64_t)(INT64_MAX - s->t_base)) {
                next = MIN(next, s->t_base + (int64_t)ns);
            }
        }
    }

    if (next != INT64_MAX) {
        timer_mod(s->timer, next);
    } else {
        timer_del(s->timer);
    }
}

static void sysctr_timer_cb(void *opaque)
{
    sysctr_update(opaque);
}

static void sysctr_set_count(Imx95SysctrState *s, uint64_t value)
{
    s->cnt_base = value & SYSCTR_CNT_MASK;
    s->t_base = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    sysctr_update(s);
}


static uint64_t sysctr_ctrl_read(void *opaque, hwaddr offset, unsigned size)
{
    Imx95SysctrState *s = opaque;

    switch (offset) {
    case CNTCR:
        return s->cntcr;
    case CNTSR:
        return s->alt_freq ? CNTSR_FCA1 : CNTSR_FCA0;
    case CNTCV0:
        return (uint32_t)sysctr_count(s);
    case CNTCV1:
        return (sysctr_count(s) >> 32) & 0xffffff;
    case CNTFID0:
        return SYSCTR_BASE_HZ;
    case CNTFID1:
        return SYSCTR_ALT_HZ;
    default:
        return 0;
    }
}

static void sysctr_ctrl_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size)
{
    Imx95SysctrState *s = opaque;
    uint32_t v = value;

    switch (offset) {
    case CNTCR: {
        uint32_t old = s->cntcr;

        if ((old & CNTCR_EN) && !(v & CNTCR_EN)) {
            s->cnt_base = sysctr_count(s);
        } else if (!(old & CNTCR_EN) && (v & CNTCR_EN)) {
            s->t_base = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        }
        s->cntcr = v & (CNTCR_EN | CNTCR_HDBG | CNTCR_FCR0 | CNTCR_FCR1);

        if ((v & CNTCR_FCR0) && !(v & CNTCR_FCR1)) {
            s->alt_freq = false;
        } else if ((v & CNTCR_FCR1) && !(v & CNTCR_FCR0)) {
            s->alt_freq = true;
        }
        sysctr_update(s);
        break;
    }
    case CNTCV0:
        sysctr_set_count(s, (sysctr_count(s) & ~0xffffffffULL) | v);
        break;
    case CNTCV1:
        sysctr_set_count(s, (sysctr_count(s) & 0xffffffffULL) |
                            ((uint64_t)(v & 0xffffff) << 32));
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------------ */
/* Compare frame                                                            */
/* ------------------------------------------------------------------------ */

static bool sysctr_cmp_decode(hwaddr offset, unsigned *frame, hwaddr *reg)
{
    if (offset < CMP_FRAME_BASE ||
        offset >= CMP_FRAME_BASE + IMX95_SYSCTR_NUM_COMPARE * CMP_FRAME_STRIDE) {
        return false;
    }
    *frame = (offset - CMP_FRAME_BASE) / CMP_FRAME_STRIDE;
    *reg = (offset - CMP_FRAME_BASE) % CMP_FRAME_STRIDE;
    return true;
}

static uint64_t sysctr_cmp_read(void *opaque, hwaddr offset, unsigned size)
{
    Imx95SysctrState *s = opaque;
    unsigned f;
    hwaddr reg;

    if (!sysctr_cmp_decode(offset, &f, &reg)) {
        return 0;               /* CNTID0 and reserved locations */
    }

    switch (reg) {
    case CMPCVL:
        return (uint32_t)s->cmpcv[f];
    case CMPCVH:
        return (s->cmpcv[f] >> 32) & 0xffffff;
    case CMPCR:
        return (s->cmpcr[f] & (CMPCR_EN | CMPCR_IMASK)) |
               (sysctr_istat(s, f, sysctr_count(s)) ? CMPCR_ISTAT : 0);
    default:
        return 0;
    }
}

static void sysctr_cmp_write(void *opaque, hwaddr offset, uint64_t value,
                             unsigned size)
{
    Imx95SysctrState *s = opaque;
    uint32_t v = value;
    unsigned f;
    hwaddr reg;

    if (!sysctr_cmp_decode(offset, &f, &reg)) {
        return;
    }

    switch (reg) {
    case CMPCVL:
        s->cmpcv[f] = (s->cmpcv[f] & ~0xffffffffULL) | v;
        break;
    case CMPCVH:
        s->cmpcv[f] = (s->cmpcv[f] & 0xffffffffULL) |
                      ((uint64_t)(v & 0xffffff) << 32);
        break;
    case CMPCR:
        /* ISTAT is read-only; clearing EN clears ISTAT and the interrupt. */
        s->cmpcr[f] = v & (CMPCR_EN | CMPCR_IMASK);
        break;
    default:
        return;
    }
    sysctr_update(s);
}

/* ------------------------------------------------------------------------ */
/* Read frame                                                               */
/* ------------------------------------------------------------------------ */

static uint64_t sysctr_read_read(void *opaque, hwaddr offset, unsigned size)
{
    Imx95SysctrState *s = opaque;

    switch (offset) {
    case CNTCV0:
        return (uint32_t)sysctr_count(s);
    case CNTCV1:
        return (sysctr_count(s) >> 32) & 0xffffff;
    default:
        return 0;               /* CNTID0 and reserved locations */
    }
}

static void sysctr_read_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size)
{
    qemu_log_mask(LOG_GUEST_ERROR,
                  "imx95.sysctr: write to the read-only read frame (0x%"
                  HWADDR_PRIx ")\n", offset);
}

#define SYSCTR_OPS(name)                                            \
    static const MemoryRegionOps sysctr_##name##_ops = {            \
        .read = sysctr_##name##_read,                               \
        .write = sysctr_##name##_write,                             \
        .endianness = DEVICE_LITTLE_ENDIAN,                         \
        .valid = { .min_access_size = 4, .max_access_size = 4 },    \
    }

SYSCTR_OPS(ctrl);
SYSCTR_OPS(cmp);
SYSCTR_OPS(read);

/* ------------------------------------------------------------------------ */

static void sysctr_reset(DeviceState *dev)
{
    Imx95SysctrState *s = IMX95_SYSCTR(dev);

    s->cntcr = 0;
    s->alt_freq = false;
    s->cnt_base = 0;
    s->t_base = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    memset(s->cmpcv, 0, sizeof(s->cmpcv));
    memset(s->cmpcr, 0, sizeof(s->cmpcr));
    sysctr_update(s);
}

static void sysctr_realize(DeviceState *dev, Error **errp)
{
    Imx95SysctrState *s = IMX95_SYSCTR(dev);

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, sysctr_timer_cb, s);
}

static void sysctr_init(Object *obj)
{
    Imx95SysctrState *s = IMX95_SYSCTR(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem[0], obj, &sysctr_ctrl_ops, s,
                          "imx95.sysctr-control", IMX95_SYSCTR_FRAME_SIZE);
    memory_region_init_io(&s->iomem[1], obj, &sysctr_cmp_ops, s,
                          "imx95.sysctr-compare", IMX95_SYSCTR_FRAME_SIZE);
    memory_region_init_io(&s->iomem[2], obj, &sysctr_read_ops, s,
                          "imx95.sysctr-read", IMX95_SYSCTR_FRAME_SIZE);
    for (unsigned i = 0; i < ARRAY_SIZE(s->iomem); i++) {
        sysbus_init_mmio(sbd, &s->iomem[i]);
    }
    for (unsigned i = 0; i < IMX95_SYSCTR_NUM_COMPARE; i++) {
        sysbus_init_irq(sbd, &s->irq[i]);
    }
}

static int sysctr_post_load(void *opaque, int version_id)
{
    sysctr_update(opaque);
    return 0;
}

static const VMStateDescription vmstate_imx95_sysctr = {
    .name = TYPE_IMX95_SYSCTR,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = sysctr_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(cntcr, Imx95SysctrState),
        VMSTATE_BOOL(alt_freq, Imx95SysctrState),
        VMSTATE_UINT64(cnt_base, Imx95SysctrState),
        VMSTATE_INT64(t_base, Imx95SysctrState),
        VMSTATE_UINT64_ARRAY(cmpcv, Imx95SysctrState,
                             IMX95_SYSCTR_NUM_COMPARE),
        VMSTATE_UINT32_ARRAY(cmpcr, Imx95SysctrState,
                             IMX95_SYSCTR_NUM_COMPARE),
        VMSTATE_END_OF_LIST()
    }
};

static void sysctr_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = sysctr_realize;
    device_class_set_legacy_reset(dc, sysctr_reset);
    dc->vmsd = &vmstate_imx95_sysctr;
    dc->desc = "i.MX95 system counter";
}

static const TypeInfo imx95_sysctr_types[] = {
    {
        .name = TYPE_IMX95_SYSCTR,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Imx95SysctrState),
        .instance_init = sysctr_init,
        .class_init = sysctr_class_init,
    },
};

DEFINE_TYPES(imx95_sysctr_types)
