/*
 * NXP i.MX95 NETC IEPRC ECAM window
 *
 * The NETC integrated endpoints are not modelled yet. Configuration reads of
 * a function that does not exist complete with all-ones data (PCIe
 * unsupported-request convention, also stated for the PCIe complex in RM
 * 160.2.10), and writes are discarded.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_IMX95_NETC_ECAM_H
#define HW_MISC_IMX95_NETC_ECAM_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_IMX95_NETC_ECAM "imx95.netc-ecam"
OBJECT_DECLARE_SIMPLE_TYPE(Imx95NetcEcamState, IMX95_NETC_ECAM)

#define IMX95_NETC_ECAM_SIZE    0x100000

struct Imx95NetcEcamState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
};

#endif /* HW_MISC_IMX95_NETC_ECAM_H */
