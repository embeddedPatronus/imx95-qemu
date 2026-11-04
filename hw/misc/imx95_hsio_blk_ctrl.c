/*
 * NXP i.MX95 HSIOMIX block control (BLK_CTRL_HSIO)
 *
 * Register model of the HSIOMIX block control. Each register has a reset
 * value and a write mask, and read-only status registers keep their reset
 * values.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "system/memory.h"
#include "hw/misc/imx95_hsio_blk_ctrl.h"

typedef struct {
    uint16_t offset;
    uint32_t reset;
    uint32_t wmask;
    const char *name;
} Imx95HsioReg;

#define QOS_MASK        0x00000707u   /* AR_QOS [10:8], AW_QOS [2:0] */
#define WAKEUP_CTRL     0x8001ff3fu   /* [31], [16:8], [5:0] */

static const Imx95HsioReg imx95_hsio_regs[] = {
    { 0x00, 0x00000023, 0x000001e3, "GPR_REG0" },
    { 0x10, 0x00000000, WAKEUP_CTRL, "USB1_WAKEUP_CTRL" },
    { 0x14, 0x00000000, WAKEUP_CTRL, "USB2_WAKEUP_CTRL" },
    { 0x18, 0x00001f10, 0x00000000, "USB1_WAKEUP_STATUS" },
    { 0x1c, 0x00000040, 0x00000000, "USB2_WAKEUP_STATUS" },
    { 0x20, 0x00000000, QOS_MASK, "USB1_AxQOS_DEFAULT" },
    { 0x24, 0x00000000, QOS_MASK, "USB1_AxQOS_PANIC" },
    { 0x28, 0x00000000, QOS_MASK, "USB2_AxQOS_DEFAULT" },
    { 0x2c, 0x00000000, QOS_MASK, "USB2_AxQOS_PANIC" },
    { 0x30, 0x00000000, QOS_MASK, "PCIE1_AxQOS_DEFAULT" },
    { 0x34, 0x00000000, QOS_MASK, "PCIE1_AxQOS_PANIC" },
    { 0x38, 0x00000000, QOS_MASK, "PCIE2_AxQOS_DEFAULT" },
    { 0x3c, 0x00000000, QOS_MASK, "PCIE2_AxQOS_PANIC" },
    { 0x40, 0x00000000, 0x00000f0f, "PCIE1_DMA_XFER_GO_TOGG" },
    { 0x44, 0x00000000, 0x00000f0f, "PCIE2_DMA_XFER_GO_TOGG" },
    { 0x50, 0x00000002, 0x00000003, "PCIE1_REG" },
    { 0x54, 0x00000002, 0x00000003, "PCIE2_REG" },
    { 0xc0, 0x000007f4, 0x000007c4, "LFAST_IO_REG" },
};

static const Imx95HsioReg *imx95_hsio_find(hwaddr offset)
{
    for (size_t i = 0; i < ARRAY_SIZE(imx95_hsio_regs); i++) {
        if (imx95_hsio_regs[i].offset == offset) {
            return &imx95_hsio_regs[i];
        }
    }
    return NULL;
}

static uint64_t imx95_hsio_blk_ctrl_read(void *opaque, hwaddr offset,
                                         unsigned size)
{
    Imx95HsioBlkCtrlState *s = opaque;

    if (!imx95_hsio_find(offset)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.hsio-blk-ctrl: read of undefined register 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }

    return s->regs[offset / 4];
}

static void imx95_hsio_blk_ctrl_write(void *opaque, hwaddr offset,
                                      uint64_t value, unsigned size)
{
    Imx95HsioBlkCtrlState *s = opaque;
    const Imx95HsioReg *r = imx95_hsio_find(offset);

    if (!r) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.hsio-blk-ctrl: write to undefined register 0x%"
                      HWADDR_PRIx "\n", offset);
        return;
    }

    if (!r->wmask) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.hsio-blk-ctrl: write to read-only register %s\n",
                      r->name);
        return;
    }

    s->regs[offset / 4] = (s->regs[offset / 4] & ~r->wmask) |
                          ((uint32_t)value & r->wmask);
}

static const MemoryRegionOps imx95_hsio_blk_ctrl_ops = {
    .read = imx95_hsio_blk_ctrl_read,
    .write = imx95_hsio_blk_ctrl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
};

static void imx95_hsio_blk_ctrl_reset(DeviceState *dev)
{
    Imx95HsioBlkCtrlState *s = IMX95_HSIO_BLK_CTRL(dev);

    memset(s->regs, 0, sizeof(s->regs));
    for (size_t i = 0; i < ARRAY_SIZE(imx95_hsio_regs); i++) {
        s->regs[imx95_hsio_regs[i].offset / 4] = imx95_hsio_regs[i].reset;
    }
}

static void imx95_hsio_blk_ctrl_init(Object *obj)
{
    Imx95HsioBlkCtrlState *s = IMX95_HSIO_BLK_CTRL(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_hsio_blk_ctrl_ops, s,
                          TYPE_IMX95_HSIO_BLK_CTRL, IMX95_HSIO_BLK_CTRL_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription vmstate_imx95_hsio_blk_ctrl = {
    .name = TYPE_IMX95_HSIO_BLK_CTRL,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, Imx95HsioBlkCtrlState,
                             IMX95_HSIO_BLK_CTRL_NUM_REGS),
        VMSTATE_END_OF_LIST()
    }
};

static void imx95_hsio_blk_ctrl_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, imx95_hsio_blk_ctrl_reset);
    dc->vmsd = &vmstate_imx95_hsio_blk_ctrl;
    dc->desc = "i.MX95 HSIOMIX block control";
}

static const TypeInfo imx95_hsio_blk_ctrl_types[] = {
    {
        .name = TYPE_IMX95_HSIO_BLK_CTRL,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Imx95HsioBlkCtrlState),
        .instance_init = imx95_hsio_blk_ctrl_init,
        .class_init = imx95_hsio_blk_ctrl_class_init,
    },
};

DEFINE_TYPES(imx95_hsio_blk_ctrl_types)
