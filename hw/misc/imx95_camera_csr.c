/*
 * NXP i.MX95 CAMERAMIX block control (CAMERA_CSR)
 *
 * Register model of the CAMERAMIX block control: the ISP, ISI and MIPI
 * CSI-2 configuration registers are stored with their reset values and
 * access types.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/core/qdev-properties.h"
#include "hw/misc/imx95_camera_csr.h"
#include "migration/vmstate.h"
#include "system/memory.h"

/* Read-only registers: writes are ignored. */
static bool imx95_camera_csr_is_ro(hwaddr offset)
{
    switch (offset) {
    case 0x018: /* Init_pending_Tx */
    case 0x058: /* CSI0_STREAM_FENCING_STATUS */
    case 0x158: /* CSI1_STREAM_FENCING_STATUS */
        return true;
    default:
        return false;
    }
}

static uint64_t imx95_camera_csr_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    Imx95CameraCsrState *s = opaque;

    if (offset >= IMX95_CAMERA_CSR_NUM_REGS * 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.camera-csr: read of undefined register 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }

    return s->regs[offset / 4];
}

static void imx95_camera_csr_write(void *opaque, hwaddr offset,
                                   uint64_t value, unsigned size)
{
    Imx95CameraCsrState *s = opaque;

    if (offset >= IMX95_CAMERA_CSR_NUM_REGS * 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.camera-csr: write to undefined register 0x%"
                      HWADDR_PRIx "\n", offset);
        return;
    }

    if (imx95_camera_csr_is_ro(offset)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.camera-csr: write to read-only register 0x%"
                      HWADDR_PRIx "\n", offset);
        return;
    }

    s->regs[offset / 4] = value;
}

static const MemoryRegionOps imx95_camera_csr_ops = {
    .read = imx95_camera_csr_read,
    .write = imx95_camera_csr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
};

static void imx95_camera_csr_reset(DeviceState *dev)
{
    Imx95CameraCsrState *s = IMX95_CAMERA_CSR(dev);

    memset(s->regs, 0, sizeof(s->regs));
    s->regs[0x4 / 4] = 0x00000303; /* ISP_AxCache_control */
    s->regs[0xc / 4] = 0x00000303; /* ISI_AxCache_control */
}

static void imx95_camera_csr_init(Object *obj)
{
    Imx95CameraCsrState *s = IMX95_CAMERA_CSR(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_camera_csr_ops, s,
                          TYPE_IMX95_CAMERA_CSR, IMX95_CAMERA_CSR_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription vmstate_imx95_camera_csr = {
    .name = TYPE_IMX95_CAMERA_CSR,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, Imx95CameraCsrState,
                             IMX95_CAMERA_CSR_NUM_REGS),
        VMSTATE_END_OF_LIST()
    }
};

static void imx95_camera_csr_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, imx95_camera_csr_reset);
    dc->vmsd = &vmstate_imx95_camera_csr;
    dc->desc = "i.MX95 CAMERAMIX block control";
}

static const TypeInfo imx95_camera_csr_types[] = {
    {
        .name = TYPE_IMX95_CAMERA_CSR,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Imx95CameraCsrState),
        .instance_init = imx95_camera_csr_init,
        .class_init = imx95_camera_csr_class_init,
    },
};

DEFINE_TYPES(imx95_camera_csr_types)
