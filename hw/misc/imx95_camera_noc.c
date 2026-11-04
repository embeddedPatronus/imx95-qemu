/*
 * NXP i.MX95 CAMERAMIX NoC (GPV) - minimal model
 *
 * Provides the five ISI/ISP write QoS generator registers that the System
 * Manager programs, as plain read/write words. Every other offset of the
 * window is reported as undefined.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "system/memory.h"
#include "hw/misc/imx95_camera_noc.h"

static const hwaddr imx95_camera_noc_qosgen[IMX95_CAMERA_NOC_NUM_QOSGEN] = {
    0xc80 + 0x18,   /* ISI_WR_U */
    0xd00 + 0x18,   /* ISI_WR_V */
    0xd80 + 0x18,   /* ISI_WR_Y */
    0xf00 + 0x18,   /* ISP_WR_0 */
    0xf80 + 0x18,   /* ISP_WR_1 */
};

static int imx95_camera_noc_index(hwaddr offset)
{
    for (int i = 0; i < IMX95_CAMERA_NOC_NUM_QOSGEN; i++) {
        if (imx95_camera_noc_qosgen[i] == offset) {
            return i;
        }
    }
    return -1;
}

static uint64_t imx95_camera_noc_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    Imx95CameraNocState *s = opaque;
    int i = imx95_camera_noc_index(offset);

    if (i < 0) {
        qemu_log_mask(LOG_UNIMP,
                      "imx95.camera-noc: read of undocumented register 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }

    return s->qosgen[i];
}

static void imx95_camera_noc_write(void *opaque, hwaddr offset,
                                   uint64_t value, unsigned size)
{
    Imx95CameraNocState *s = opaque;
    int i = imx95_camera_noc_index(offset);

    if (i < 0) {
        qemu_log_mask(LOG_UNIMP,
                      "imx95.camera-noc: write to undocumented register 0x%"
                      HWADDR_PRIx " = 0x%" PRIx64 "\n", offset, value);
        return;
    }

    s->qosgen[i] = value;
}

static const MemoryRegionOps imx95_camera_noc_ops = {
    .read = imx95_camera_noc_read,
    .write = imx95_camera_noc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
};

static void imx95_camera_noc_reset(DeviceState *dev)
{
    Imx95CameraNocState *s = IMX95_CAMERA_NOC(dev);

    memset(s->qosgen, 0, sizeof(s->qosgen));
}

static void imx95_camera_noc_init(Object *obj)
{
    Imx95CameraNocState *s = IMX95_CAMERA_NOC(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_camera_noc_ops, s,
                          TYPE_IMX95_CAMERA_NOC, IMX95_CAMERA_NOC_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription vmstate_imx95_camera_noc = {
    .name = TYPE_IMX95_CAMERA_NOC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(qosgen, Imx95CameraNocState,
                             IMX95_CAMERA_NOC_NUM_QOSGEN),
        VMSTATE_END_OF_LIST()
    }
};

static void imx95_camera_noc_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, imx95_camera_noc_reset);
    dc->vmsd = &vmstate_imx95_camera_noc;
    dc->desc = "i.MX95 CAMERAMIX NoC (minimal)";
}

static const TypeInfo imx95_camera_noc_types[] = {
    {
        .name = TYPE_IMX95_CAMERA_NOC,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Imx95CameraNocState),
        .instance_init = imx95_camera_noc_init,
        .class_init = imx95_camera_noc_class_init,
    },
};

DEFINE_TYPES(imx95_camera_noc_types)
