/*
 * NXP i.MX95 AHAB container-set parser
 *
 * Finds the System Manager and Cortex-A55 SPL images in the AHAB container
 * sets of a boot medium.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qapi/error.h"
#include "hw/arm/imx95-container.h"
#include "trace.h"

static uint8_t container_version(const uint8_t *h)
{
    return h[0];
}

static uint32_t container_length(const uint8_t *h)
{
    return lduw_le_p(h + 1);
}

static uint8_t container_tag(const uint8_t *h)
{
    return h[3];
}

static unsigned int container_num_images(const uint8_t *h)
{
    return h[11];
}

static bool container_header_valid(const uint8_t *h,
                                   unsigned int set_version)
{
    if (container_tag(h) != IMX95_CNTR_TAG &&
        !(set_version == 2 && container_tag(h) == IMX95_CNTR_V2X_TAG)) {
        return false;
    }
    return container_length(h) >= IMX95_CNTR_HEADER_SIZE;
}

static bool range_valid(uint64_t offset, uint64_t length, uint64_t total)
{
    return offset <= total && length <= total - offset;
}

static bool u64_add_checked(uint64_t a, uint64_t b, uint64_t *result)
{
    if (b > UINT64_MAX - a) {
        return false;
    }
    *result = a + b;
    return true;
}

static bool image_is_executable_on(const Imx95ContainerImage *img,
                                   uint32_t core)
{
    return img->size != 0 &&
           (img->flags & IMX95_IMG_TYPE_MASK) == IMX95_IMG_TYPE_EXECUTABLE &&
           (img->flags & IMX95_IMG_CORE_MASK) == core;
}

static bool image_is_dummy(const Imx95ContainerImage *img)
{
    return (img->flags & IMX95_IMG_TYPE_MASK) == IMX95_IMG_TYPE_DUMMY;
}

bool imx95_container_find_images(const Imx95ContainerMedia *m,
                                 uint64_t set_base, bool lenient,
                                 Imx95ContainerSelection *sel, Error **errp)
{
    uint8_t header[IMX95_CNTR_HEADER_SIZE];
    uint8_t entries[IMX95_MAX_IMAGES_PER_CNTR * IMX95_BOOT_IMG_ENTRY_SIZE];
    uint64_t container_off = 0;
    unsigned int set_version;
    bool have_preferred_cm33 = false;

    memset(sel, 0, sizeof(*sel));

    if (!range_valid(set_base, sizeof(header), m->size)) {
        error_setg(errp, "no container set header at offset 0x%" PRIx64,
                   set_base);
        return false;
    }
    if (!m->read(m->opaque, set_base, header, sizeof(header), errp)) {
        return false;
    }

    set_version = container_version(header);
    if (set_version != 0 && set_version != 2) {
        error_setg(errp, "unsupported container-set version %u", set_version);
        return false;
    }
    sel->set_version = set_version;
    trace_imx95_container_set(set_version);

    for (unsigned int c = 0; c < IMX95_MAX_ROM_CONTAINERS; c++) {
        uint64_t header_off;
        uint64_t entries_off;
        uint32_t length;
        unsigned num_images;
        size_t entries_size;

        if (set_version == 2) {
            container_off = (uint64_t)c * IMX95_CNTR_V2_SLOT_SIZE;
        }

        if (!u64_add_checked(set_base, container_off, &header_off) ||
            !range_valid(header_off, sizeof(header), m->size)) {
            if (lenient || c > 0) {
                break;
            }
            error_setg(errp, "container %u header lies outside the boot medium",
                       c + 1);
            return false;
        }
        if (!m->read(m->opaque, header_off, header, sizeof(header), errp)) {
            return false;
        }

        if (!container_header_valid(header, set_version)) {
            if (c == 0 && !lenient) {
                error_setg(errp, "invalid first container header at offset "
                           "0x%" PRIx64, header_off);
                return false;
            }
            break;
        }

        length = container_length(header);
        num_images = container_num_images(header);
        trace_imx95_container_header(c + 1, header_off,
                                       container_version(header),
                                     container_tag(header), length, num_images);

        if (num_images == 0 || num_images > IMX95_MAX_IMAGES_PER_CNTR) {
            if (lenient) {
                break;
            }
            error_setg(errp, "container %u has an invalid image count (%u)",
                       c + 1, num_images);
            return false;
        }

        entries_size = (size_t)num_images * IMX95_BOOT_IMG_ENTRY_SIZE;
        if (length < IMX95_CNTR_HEADER_SIZE + entries_size) {
            error_setg(errp, "container %u header length 0x%x cannot hold %u "
                       "image entries", c + 1, length, num_images);
            return false;
        }
        if (set_version == 2 && length > IMX95_CNTR_V2_SLOT_SIZE) {
            error_setg(errp, "container %u header length 0x%x exceeds the "
                       "version 2 slot size 0x%x", c + 1, length,
                       IMX95_CNTR_V2_SLOT_SIZE);
            return false;
        }

        if (!u64_add_checked(header_off, IMX95_CNTR_HEADER_SIZE,
                             &entries_off) ||
            !range_valid(entries_off, entries_size, m->size)) {
            if (lenient) {
                break;
            }
            error_setg(errp, "container %u image array lies outside the boot "
                       "medium", c + 1);
            return false;
        }
        if (!m->read(m->opaque, entries_off, entries, entries_size, errp)) {
            return false;
        }

        for (unsigned int i = 0; i < num_images; i++) {
            const uint8_t *e = entries + i * IMX95_BOOT_IMG_ENTRY_SIZE;
            Imx95ContainerImage img = {
                .size = ldl_le_p(e + 0x04),
                .load_addr = ldq_le_p(e + 0x08),
                .entry_addr = ldq_le_p(e + 0x10),
                .flags = ldl_le_p(e + 0x18),
                .metadata = ldl_le_p(e + 0x1c),
                .container_index = c + 1,
                .image_index = i + 1,
            };
            uint64_t base;

            if (!u64_add_checked(set_base, container_off, &base) ||
                !u64_add_checked(base, ldl_le_p(e + 0x00), &img.offset)) {
                if (lenient) {
                    continue;
                }
                error_setg(errp, "image c%u.i%u offset overflows",
                           img.container_index, img.image_index);
                return false;
            }

            trace_imx95_container_image(img.container_index, img.image_index,
                                        img.offset, img.size, img.load_addr,
                                        img.entry_addr, img.flags);

            if (!range_valid(img.offset, img.size, m->size)) {
                if (lenient) {
                    continue;
                }
                error_setg(errp, "image c%u.i%u (offset 0x%" PRIx64
                           ", size 0x%x) lies outside the boot medium",
                           img.container_index, img.image_index,
                           img.offset, img.size);
                return false;
            }

            if (image_is_dummy(&img) && img.size == 0) {
                if (sel->have_secondary_set &&
                    sel->secondary_set_offset != img.offset) {
                    error_setg(errp, "conflicting secondary container set "
                               "offsets 0x%" PRIx64 " and 0x%" PRIx64,
                               sel->secondary_set_offset, img.offset);
                    return false;
                }
                sel->secondary_set_offset = img.offset;
                sel->have_secondary_set = true;
                continue;
            }

            if (image_is_executable_on(&img, IMX95_IMG_CORE_CM33)) {
                bool preferred = img.load_addr == IMX95_SM_IMAGE_BASE;

                if (!sel->have_cm33 ||
                    (preferred && !have_preferred_cm33)) {
                    sel->cm33 = img;
                    sel->have_cm33 = true;
                    have_preferred_cm33 = preferred;
                }
                continue;
            }

            if (image_is_executable_on(&img, IMX95_IMG_CORE_CM7)) {
                if (!sel->have_cm7) {
                    sel->cm7 = img;
                    sel->have_cm7 = true;
                }
                continue;
            }

            /*
             * The first executable Cortex-A55 image of the ROM container set
             * is the SPL; its addresses come from the container.
             */
            if (image_is_executable_on(&img, IMX95_IMG_CORE_CA55) &&
                !sel->have_a55_spl) {
                sel->a55_spl = img;
                sel->have_a55_spl = true;
            }
        }

        if (set_version == 0) {
            container_off += QEMU_ALIGN_UP(length, IMX95_CNTR_ALIGN_V0);
        }
    }

    if (!sel->have_cm33) {
        error_setg(errp, "no Cortex-M33 executable image in the container "
                   "set");
        return false;
    }
    return true;
}

bool imx95_container_validate_set(const Imx95ContainerMedia *m,
                                  uint64_t offset, Error **errp)
{
    uint8_t header[IMX95_CNTR_HEADER_SIZE];
    uint32_t length;
    unsigned int version;
    unsigned int num_images;

    if (!range_valid(offset, sizeof(header), m->size)) {
        error_setg(errp, "secondary container set header lies outside the boot "
                   "medium (offset 0x%" PRIx64 ")", offset);
        return false;
    }
    if (!m->read(m->opaque, offset, header, sizeof(header), errp)) {
        return false;
    }

    version = container_version(header);
    length = container_length(header);
    num_images = container_num_images(header);

    if (container_tag(header) != IMX95_CNTR_TAG) {
        error_setg(errp, "invalid secondary container tag 0x%02x at offset "
                   "0x%" PRIx64, container_tag(header), offset);
        return false;
    }
    if (version != 0 && version != 2) {
        error_setg(errp, "invalid secondary container version %u at offset "
                   "0x%" PRIx64, version, offset);
        return false;
    }
    if (num_images == 0 || num_images > IMX95_MAX_IMAGES_PER_CNTR) {
        error_setg(errp, "invalid secondary image count %u at offset 0x%"
                   PRIx64, num_images, offset);
        return false;
    }
    if (length < IMX95_CNTR_HEADER_SIZE +
                 (size_t)num_images * IMX95_BOOT_IMG_ENTRY_SIZE) {
        error_setg(errp, "invalid secondary header length 0x%x for %u images",
                   length, num_images);
        return false;
    }
    if (!range_valid(offset, length, m->size)) {
        error_setg(errp, "secondary container header lies outside the boot "
                   "medium (offset 0x%" PRIx64 ", length 0x%x)",
                   offset, length);
        return false;
    }

    trace_imx95_container_secondary_set(offset, version, container_tag(header),
                                        length, num_images);
    return true;
}

/*
 * Basic vector-table sanity check
 */
bool imx95_container_cm33_vectors_valid(const uint8_t *payload, uint32_t size)
{
    uint32_t initial_msp;
    uint32_t reset_vector;

    if (size < 8) {
        return false;
    }
    initial_msp = ldl_le_p(payload);
    reset_vector = ldl_le_p(payload + 4);

    return initial_msp != 0 && reset_vector != 0 && (reset_vector & 1) != 0;
}

