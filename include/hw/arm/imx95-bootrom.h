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

/* Boot device as BOOT_MODE[3:0] picks it: x010 eMMC, x100 NOR */
typedef enum Imx95BootDevice {
    IMX95_BOOT_DEVICE_EMMC,
    IMX95_BOOT_DEVICE_SD,
    IMX95_BOOT_DEVICE_NOR,
} Imx95BootDevice;

typedef struct Imx95BootRomInfo {
    uint8_t dev_type;
    uint8_t dev_inst;
    uint32_t dev_page_size;

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

/* Load from @medium of boot device @dev, or from @kernel_filename */
bool imx95_bootrom_load(Imx95BootRomInfo *info, Imx95BootDevice dev,
                        BlockBackend *medium, const char *kernel_filename,
                        uint32_t boot_cfg8, Error **errp);

void imx95_bootrom_add_handover(const Imx95BootRomInfo *info);

#endif /* HW_ARM_IMX95_BOOTROM_H */
