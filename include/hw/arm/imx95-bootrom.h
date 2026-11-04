/*
 * NXP i.MX95 boot ROM emulation: interface
 *
 * Declares the information the boot ROM model learns about the Cortex-A55
 * SPL and the container sets, and the functions that load the images and
 * publish the handover records.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ARM_IMX95_BOOTROM_H
#define HW_ARM_IMX95_BOOTROM_H

#include "qemu/osdep.h"
#include "exec/hwaddr.h"
#include "qapi/error.h"

typedef struct Imx95BootRomInfo {
    bool spl_loaded;
    hwaddr spl_load_addr;
    hwaddr spl_entry_addr;
    uint32_t spl_size;
    unsigned spl_container_index;
    unsigned spl_image_index;
    bool secondary_set_valid;
    uint32_t secondary_set_offset;
} Imx95BootRomInfo;

bool imx95_bootrom_load(Imx95BootRomInfo *info, BlockBackend *sd,
                        const char *kernel_filename, Error **errp);

void imx95_bootrom_add_handover(const Imx95BootRomInfo *info);

#endif /* HW_ARM_IMX95_BOOTROM_H */
