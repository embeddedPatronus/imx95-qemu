/*
 * NXP i.MX95 boot ROM emulation (high-level)
 *
 * The real boot ROM reads a container set from the boot medium, hands the
 * System Manager (Cortex-M33) image and the Cortex-A55 SPL image to the
 * right cores and leaves a "handover"/"passover" record in M33 TCM. This file
 * does the same on the host side: it finds the images with the container
 * parser (imx95-container.c), registers them with the ROM loader and
 * publishes the records. It owns no device state; everything it learns is
 * returned in Imx95BootRomInfo, and errors are returned to the caller.
 *
 * Boot flow reference: i.MX 95 Applications Processor Reference Manual,
 * Rev. 5, chapter 13 (System Boot).
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/core/loader.h"
#include "system/block-backend.h"
#include "hw/arm/imx95-bootrom.h"
#include "hw/arm/imx95-container.h"
#include "trace.h"

/* The System Manager image must fit the M33 code TCM window. */
#define IMX95_SM_IMAGE_MAX_SIZE      (256 * KiB)

/* Offset of the first container set on an SD card (RM 13.5.1.2, table 49). */
#define IMX95_MMCSD_CONTAINER_OFFSET 0x8000ULL

#define IMX95_ROM_HANDOVER_BASE      0x2003DC00
#define IMX95_ROM_HANDOVER_SIZE      0x100
#define IMX95_ROM_HANDOVER_BARKER    0xC0FFEE16
#define IMX95_ROM_HANDOVER_VER       0x0002
#define IMX95_ROM_PASSOVER_BASE      0x2003DE00
#define IMX95_ROM_PASSOVER_SCMI_WORDS           15
#define IMX95_ROM_PASSOVER_SCMI_SIZE        \
    (IMX95_ROM_PASSOVER_SCMI_WORDS * sizeof(uint32_t))
#define IMX95_ROM_PASSOVER_BOOT_MODE_OFF    0x04
#define IMX95_ROM_PASSOVER_CARD_MODE_OFF    0x08
#define IMX95_ROM_PASSOVER_BOOT_STAGE_OFF   0x18
#define IMX95_ROM_PASSOVER_IMG_SET_SEL_OFF  0x19
#define IMX95_ROM_PASSOVER_IMG_SET_END_OFF  0x1c
#define IMX95_ROM_PASSOVER_ROM_VERSION_OFF  0x20
#define IMX95_ROM_PASSOVER_BOOT_STATE_OFF   0x24
#define IMX95_ROM_PASSOVER_BOOT_INST_OFF    0x25
#define IMX95_ROM_PASSOVER_BOOT_TYPE_OFF    0x26
#define IMX95_ROM_PASSOVER_DEV_PAGE_OFF     0x28
#define IMX95_ROM_PASSOVER_CNT_HDR_OFF      0x2c
#define IMX95_ROM_PASSOVER_IMG_OFS_OFF      0x30

/* Boot modes reported in the passover record (DEV_SM_ROM_BM_* in the SM). */
#define IMX95_ROM_BOOT_MODE_FUSE         0x0U
#define IMX95_ROM_BOOT_MODE_USB          0x1U
#define IMX95_ROM_BOOT_MODE_NORMAL       0x2U
#define IMX95_ROM_CARD_ADDR_MODE_DEFAULT 0U
#define IMX95_ROM_BOOT_DEV_STATE_ACTIVE  1U
#define IMX95_ROM_BOOT_DEV_INSTANCE_0    0U
#define IMX95_ROM_BOOT_DEV_TYPE_MMC      2U
#define IMX95_ROM_IMG_TYPE_EXEC      0U
#define IMX95_ROM_CPU_A55C0          2U
#define IMX95_ROM_MSEL_DEFAULT       0U

/* ------------------------------------------------------------------------ */
/* Boot media                                                               */
/* ------------------------------------------------------------------------ */

static bool rom_blk_read(void *opaque, uint64_t offset, void *buf, size_t len,
                         Error **errp)
{
    int ret = blk_pread(opaque, offset, len, buf, 0);

    if (ret < 0) {
        error_setg_errno(errp, -ret, "read of 0x%zx bytes at offset 0x%"
                         PRIx64 " failed", len, offset);
        return false;
    }
    return true;
}

typedef struct RomBuffer {
    const uint8_t *data;
    size_t len;
} RomBuffer;

static bool rom_buffer_read(void *opaque, uint64_t offset, void *buf,
                            size_t len, Error **errp)
{
    const RomBuffer *b = opaque;

    if (offset > b->len || len > b->len - offset) {
        error_setg(errp, "read outside the firmware file");
        return false;
    }
    memcpy(buf, b->data + offset, len);
    return true;
}

static void rom_note_spl(Imx95BootRomInfo *info, const Imx95ContainerImage *spl)
{
    info->spl_loaded = true;
    info->spl_load_addr = spl->load_addr;
    info->spl_entry_addr = spl->entry_addr;
    info->spl_size = spl->size;
    info->spl_container_index = spl->container_index;
    info->spl_image_index = spl->image_index;
}

static bool rom_load_from_file(Imx95BootRomInfo *info, const char *filename,
                               Error **errp)
{
    g_autofree gchar *contents = NULL;
    gsize len = 0;
    GError *gerr = NULL;
    RomBuffer buf;
    Imx95ContainerMedia media;
    Imx95ContainerSelection sel;

    if (!g_file_get_contents(filename, &contents, &len, &gerr)) {
        error_setg(errp, "failed to read %s: %s", filename, gerr->message);
        g_error_free(gerr);
        return false;
    }

    buf.data = (const uint8_t *)contents;
    buf.len = len;
    media.read = rom_buffer_read;
    media.opaque = &buf;
    media.size = len;

    if (!imx95_container_find_images(&media, 0, true, &sel, errp)) {
        return false;
    }

    trace_imx95_bootrom_image("cm33", sel.cm33.container_index,
                              sel.cm33.image_index, sel.cm33.offset,
                              sel.cm33.size, sel.cm33.load_addr);
    rom_add_blob_fixed("imx95.sm.container", contents + sel.cm33.offset,
                       sel.cm33.size, sel.cm33.load_addr);

    if (sel.have_a55_spl) {
        trace_imx95_bootrom_image("a55-spl", sel.a55_spl.container_index,
                                  sel.a55_spl.image_index, sel.a55_spl.offset,
                                  sel.a55_spl.size, sel.a55_spl.load_addr);
        rom_note_spl(info, &sel.a55_spl);
        rom_add_blob_fixed("imx95.a55-spl.container",
                           contents + sel.a55_spl.offset, sel.a55_spl.size,
                           sel.a55_spl.load_addr);
    } else {
        trace_imx95_bootrom_no_spl();
    }
    return true;
}

static bool rom_load_payload(BlockBackend *blk, const Imx95ContainerImage *img,
                             const char *blob_name, const char *what,
                             bool check_cm33_vectors, Error **errp)
{
    g_autofree uint8_t *payload = NULL;

    if (img->size == 0) {
        error_setg(errp, "selected %s image c%u.i%u has zero size", what,
                   img->container_index, img->image_index);
        return false;
    }

    payload = g_malloc(img->size);
    if (!rom_blk_read(blk, img->offset, payload, img->size, errp)) {
        return false;
    }

    if (check_cm33_vectors &&
        !imx95_container_cm33_vectors_valid(payload, img->size)) {
        error_setg(errp, "invalid Cortex-M33 vectors in image c%u.i%u",
                   img->container_index, img->image_index);
        return false;
    }

    trace_imx95_bootrom_image(what, img->container_index, img->image_index,
                              img->offset, img->size, img->load_addr);
    rom_add_blob_fixed(blob_name, payload, img->size, img->load_addr);
    return true;
}

static bool rom_load_from_sd(Imx95BootRomInfo *info, BlockBackend *blk,
                             Error **errp)
{
    int64_t sd_size = blk_getlength(blk);
    Imx95ContainerMedia media = { .read = rom_blk_read, .opaque = blk };
    Imx95ContainerSelection sel;

    if (sd_size <= 0) {
        error_setg(errp, "the SD image has zero or unknown size");
        return false;
    }
    media.size = sd_size;

    if (!imx95_container_find_images(&media, IMX95_MMCSD_CONTAINER_OFFSET,
                                     false, &sel, errp)) {
        error_prepend(errp, "no bootable container set at SD offset 0x%llx: ",
                      IMX95_MMCSD_CONTAINER_OFFSET);
        return false;
    }

    if (!sel.have_secondary_set) {
        error_setg(errp, "the primary container set does not describe a "
                   "secondary container set");
        return false;
    }
    if (sel.secondary_set_offset > UINT32_MAX) {
        error_setg(errp, "secondary container set offset 0x%" PRIx64
                   " does not fit the ROM passover record",
                   sel.secondary_set_offset);
        return false;
    }
    if (!imx95_container_validate_set(&media, sel.secondary_set_offset,
                                      errp)) {
        return false;
    }
    info->secondary_set_valid = true;
    info->secondary_set_offset = (uint32_t)sel.secondary_set_offset;

    if (!rom_load_payload(blk, &sel.cm33, "imx95.sm.sd", "cm33", true, errp)) {
        return false;
    }

    if (!sel.have_a55_spl) {
        error_setg(errp, "no Cortex-A55 SPL image in the primary container "
                   "set");
        return false;
    }
    if (!rom_load_payload(blk, &sel.a55_spl, "imx95.a55-spl.sd", "a55-spl",
                          false, errp)) {
        return false;
    }
    rom_note_spl(info, &sel.a55_spl);
    return true;
}

static bool rom_load(Imx95BootRomInfo *info, BlockBackend *sd,
                     const char *kernel_filename, Error **errp)
{
    if (kernel_filename) {
        Error *local_err = NULL;
        ssize_t size;

        trace_imx95_bootrom_source("file");
        if (rom_load_from_file(info, kernel_filename, &local_err)) {
            return true;
        }

        trace_imx95_bootrom_not_container(error_get_pretty(local_err));
        error_free(local_err);
        memset(info, 0, sizeof(*info));

        size = load_image_targphys(kernel_filename, IMX95_SM_IMAGE_BASE,
                                   IMX95_SM_IMAGE_MAX_SIZE, errp);
        if (size < 0) {
            return false;
        }
        trace_imx95_bootrom_raw_sm(kernel_filename, IMX95_SM_IMAGE_BASE, size);
        return true;
    }

    trace_imx95_bootrom_source("sd");
    if (!sd) {
        error_setg(errp, "no boot medium: provide either -kernel flash.bin "
                   "or -drive if=sd,index=0,file=<sd.img>");
        return false;
    }
    return rom_load_from_sd(info, sd, errp);
}

bool imx95_bootrom_load(Imx95BootRomInfo *info, BlockBackend *sd,
                        const char *kernel_filename, Error **errp)
{
    Error *local_err = NULL;

    memset(info, 0, sizeof(*info));

    /*
     * Collect the error locally: error_prepend() needs it to be set, and
     * with errp == &error_fatal error_setg() would already have exited.
     */
    if (!rom_load(info, sd, kernel_filename, &local_err)) {
        error_propagate_prepend(errp, local_err, "imx95 boot ROM: ");
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* Handover and passover records                                            */
/* ------------------------------------------------------------------------ */

static uint32_t rom_handover_flags(uint32_t cpu, uint32_t type, uint32_t msel,
                                   uint32_t flags)
{
    return ((cpu & 0xff) << 0) | ((type & 0xff) << 8) |
           ((msel & 0xff) << 16) | ((flags & 0xff) << 24);
}

void imx95_bootrom_add_handover(const Imx95BootRomInfo *info)
{
    uint8_t handover[IMX95_ROM_HANDOVER_SIZE];
    uint8_t passover[IMX95_ROM_PASSOVER_SCMI_SIZE];
    uint32_t img_flags;

    if (!info->spl_loaded || !info->secondary_set_valid) {
        /* Only a full ROM boot from the SD card leaves these records. */
        trace_imx95_bootrom_no_handover(info->spl_loaded,
                                        info->secondary_set_valid);
        return;
    }

    memset(handover, 0, sizeof(handover));
    memset(passover, 0, sizeof(passover));

    stl_le_p(handover + 0x00, IMX95_ROM_HANDOVER_BARKER);
    stw_le_p(handover + 0x04, IMX95_ROM_HANDOVER_VER);
    stw_le_p(handover + 0x06, IMX95_ROM_HANDOVER_SIZE);
    handover[0x08] = 1;
    stl_le_p(handover + 0x0c, 0);

    img_flags = rom_handover_flags(IMX95_ROM_CPU_A55C0, IMX95_ROM_IMG_TYPE_EXEC,
                                   IMX95_ROM_MSEL_DEFAULT, 0);
    stq_le_p(handover + 0x10, info->spl_entry_addr);
    stw_le_p(handover + 0x18, 0);
    stl_le_p(handover + 0x1c, img_flags);

    stl_le_p(passover + 0x00, 0x0280504f);
    stl_le_p(passover + IMX95_ROM_PASSOVER_BOOT_MODE_OFF,
             IMX95_ROM_BOOT_MODE_NORMAL);
    stl_le_p(passover + IMX95_ROM_PASSOVER_CARD_MODE_OFF,
             IMX95_ROM_CARD_ADDR_MODE_DEFAULT);
    passover[IMX95_ROM_PASSOVER_BOOT_STATE_OFF] =
        IMX95_ROM_BOOT_DEV_STATE_ACTIVE;
    passover[IMX95_ROM_PASSOVER_BOOT_INST_OFF] = IMX95_ROM_BOOT_DEV_INSTANCE_0;
    passover[IMX95_ROM_PASSOVER_BOOT_TYPE_OFF] = IMX95_ROM_BOOT_DEV_TYPE_MMC;
    passover[IMX95_ROM_PASSOVER_BOOT_STAGE_OFF] = 0x6;
    passover[IMX95_ROM_PASSOVER_IMG_SET_SEL_OFF] = 0;
    stl_le_p(passover + IMX95_ROM_PASSOVER_IMG_SET_END_OFF,
             info->secondary_set_offset);
    stl_le_p(passover + IMX95_ROM_PASSOVER_ROM_VERSION_OFF,
             IMX95_ROM_HANDOVER_VER);
    stl_le_p(passover + IMX95_ROM_PASSOVER_DEV_PAGE_OFF, 512);
    stl_le_p(passover + IMX95_ROM_PASSOVER_CNT_HDR_OFF,
             IMX95_MMCSD_CONTAINER_OFFSET);
    stl_le_p(passover + IMX95_ROM_PASSOVER_IMG_OFS_OFF,
             IMX95_MMCSD_CONTAINER_OFFSET);

    rom_add_blob_fixed("imx95.rom-handover", handover, sizeof(handover),
                       IMX95_ROM_HANDOVER_BASE);
    rom_add_blob_fixed("imx95.rom-passover", passover, sizeof(passover),
                       IMX95_ROM_PASSOVER_BASE);

    trace_imx95_bootrom_handover(info->spl_entry_addr, img_flags,
                                 info->secondary_set_offset);
}
