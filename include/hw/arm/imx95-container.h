/*
 * NXP i.MX95 AHAB container-set parser
 *
 * The i.MX95 boot ROM finds the System Manager (Cortex-M33) and the Cortex-A55
 * SPL images by walking a "container set" on the boot medium. This file knows
 * that format and nothing else: it reads through a callback, so the same
 * parser serves a block device and an in-memory firmware file (the approach of
 * hw/core/loader-fit.c, which keeps the FIT parser apart from the machines
 * that use it).
 *
 * Format reference: i.MX 95 Applications Processor Reference Manual, Rev. 5,
 * chapter 13.5 (System Boot: AHAB 2.0 image container format).
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ARM_IMX95_CONTAINER_H
#define HW_ARM_IMX95_CONTAINER_H

#include "qemu/osdep.h"
#include "qapi/error.h"

#define IMX95_CNTR_TAG               0x87
#define IMX95_CNTR_V2X_TAG           0x82
#define IMX95_CNTR_HEADER_SIZE       0x10
#define IMX95_BOOT_IMG_ENTRY_SIZE    0x80
#define IMX95_MAX_ROM_CONTAINERS     3
#define IMX95_MAX_IMAGES_PER_CNTR    8
#define IMX95_CNTR_ALIGN_V0          0x400
#define IMX95_CNTR_V2_SLOT_SIZE      0x4000

#define IMX95_IMG_TYPE_MASK          0x0000000f
#define IMX95_IMG_CORE_MASK          0x000000f0
#define IMX95_IMG_TYPE_EXECUTABLE    0x3
#define IMX95_IMG_TYPE_DUMMY         0xe
#define IMX95_IMG_CORE_CM33          0x10
#define IMX95_IMG_CORE_CA55          0x20

#define IMX95_SM_IMAGE_BASE          0x1ffc0000

typedef bool (*Imx95ContainerReadFn)(void *opaque, uint64_t offset,
                                     void *buf, size_t len, Error **errp);

typedef struct Imx95ContainerMedia {
    Imx95ContainerReadFn read;
    void *opaque;
    uint64_t size;
} Imx95ContainerMedia;

typedef struct Imx95ContainerImage {
    uint64_t offset;            /* absolute offset on the medium */
    uint32_t size;
    uint64_t load_addr;
    uint64_t entry_addr;
    uint32_t flags;
    uint32_t metadata;
    unsigned container_index;   /* 1-based, as in the boot log */
    unsigned image_index;
} Imx95ContainerImage;

typedef struct Imx95ContainerSelection {
    bool have_cm33;
    bool have_a55_spl;
    bool have_secondary_set;
    unsigned set_version;
    uint64_t secondary_set_offset;
    Imx95ContainerImage cm33;
    Imx95ContainerImage a55_spl;
} Imx95ContainerSelection;

bool imx95_container_find_images(const Imx95ContainerMedia *m,
                                 uint64_t set_base, bool lenient,
                                 Imx95ContainerSelection *sel, Error **errp);

bool imx95_container_validate_set(const Imx95ContainerMedia *m,
                                  uint64_t offset, Error **errp);

bool imx95_container_cm33_vectors_valid(const uint8_t *payload, uint32_t size);

#endif /* HW_ARM_IMX95_CONTAINER_H */
