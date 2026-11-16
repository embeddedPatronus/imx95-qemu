/*
 * NXP i.MX95 PCIe root complex
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_PCI_HOST_IMX95_PCIE_HOST_H
#define HW_PCI_HOST_IMX95_PCIE_HOST_H

#include "qemu/units.h"
#include "hw/core/sysbus.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pci_host.h"
#include "qom/object.h"

#define TYPE_IMX95_PCIE_ROOT_BUS "imx95.pcie-root-bus"
OBJECT_DECLARE_SIMPLE_TYPE(Imx95PcieRootBus, IMX95_PCIE_ROOT_BUS)

#define TYPE_IMX95_PCIE_HOST "imx95.pcie-host"
OBJECT_DECLARE_SIMPLE_TYPE(Imx95PcieHost, IMX95_PCIE_HOST)

#define TYPE_IMX95_PCIE_ROOT "imx95.pcie-root"
OBJECT_DECLARE_SIMPLE_TYPE(Imx95PcieRoot, IMX95_PCIE_ROOT)

#define IMX95_PCIE_NUM_ATU       8
#define IMX95_PCIE_IO_SIZE       (1 * MiB)

/* port logic registers 0x700 to 0x8ff kept as storage */
#define IMX95_PCIE_PL_WORDS      (0x200 / 4)

struct Imx95PcieRootBus {
    PCIBus parent;
};

typedef struct Imx95PcieAtu {
    uint32_t ctrl1;
    uint32_t ctrl2;
    uint64_t base;
    uint32_t limit_lo;
    uint32_t limit_hi;
    uint64_t target;
} Imx95PcieAtu;

typedef struct Imx95PcieMap {
    unsigned kind;
    uint64_t base;
    uint64_t size;
    uint64_t offset;
} Imx95PcieMap;

typedef struct Imx95PcieOb {
    Imx95PcieHost *host;
    unsigned index;
    MemoryRegion mem;
    MemoryRegion io;
    MemoryRegion cfg;
    Imx95PcieMap applied;
} Imx95PcieOb;

typedef struct Imx95PcieIb {
    MemoryRegion mem;
    Imx95PcieMap applied;
} Imx95PcieIb;

struct Imx95PcieRoot {
    PCIBridge parent_obj;

    uint32_t pl[IMX95_PCIE_PL_WORDS];
};

struct Imx95PcieHost {
    PCIHostState parent_obj;

    Imx95PcieRoot root;

    char *bus_name;
    uint32_t domain_nr;
    char root_path[16];

    MemoryRegion dbi;
    MemoryRegion atu_mmio;
    MemoryRegion io;
    MemoryRegion memory;
    MemoryRegion address_space_root;
    MemoryRegion inbound;
    AddressSpace address_space;

    qemu_irq irqs[4];
    qemu_irq msi;
    bool ltssm_en;

    Imx95PcieAtu ob[IMX95_PCIE_NUM_ATU];
    Imx95PcieAtu ib[IMX95_PCIE_NUM_ATU];
    Imx95PcieOb ob_region[IMX95_PCIE_NUM_ATU];
    Imx95PcieIb ib_region[IMX95_PCIE_NUM_ATU];
};

#endif /* HW_PCI_HOST_IMX95_PCIE_HOST_H */
