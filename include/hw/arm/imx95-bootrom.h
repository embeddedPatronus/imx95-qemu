/*
 * NXP i.MX95 boot ROM emulation
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
    bool m7_loaded;
    hwaddr m7_load_addr;
    hwaddr m7_entry_addr;
    uint32_t m7_size;

    unsigned img_set;
    uint32_t img_set_offset;
    uint32_t img_set_end;
} Imx95BootRomInfo;

/*
 * @boot_cfg8: the BOOT_CFG8 fuse word, which holds secondary boot offset
 */
bool imx95_bootrom_load(Imx95BootRomInfo *info, BlockBackend *sd,
                        const char *kernel_filename, uint32_t boot_cfg8,
                        Error **errp);

void imx95_bootrom_add_handover(const Imx95BootRomInfo *info);

#endif /* HW_ARM_IMX95_BOOTROM_H */
