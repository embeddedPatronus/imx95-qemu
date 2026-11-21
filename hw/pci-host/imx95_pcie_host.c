/*
 * NXP i.MX95 PCIe root complex
 *
 * DesignWare PCIe controller that the i.MX95 uses for PCIe0 and PCIe1 in root complex mode
 * the link comes up only when LTSSM_EN is set in the SerDes subsystem and a device is present.
 * An empty slot reports link down.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/bitops.h"
#include "hw/pci/msi.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pci_host.h"
#include "hw/pci/pcie_port.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "system/address-spaces.h"
#include "hw/pci-host/imx95_pcie_host.h"

/* Port logic registers, offsets in the DBI space */
#define PL_BASE                     0x700
#define PL_PORT_LINK_CONTROL        0x710
#define PL_DEBUG0                   0x728
#define PL_DEBUG1                   0x72c
#define PL_DEBUG1_LINK_UP           BIT(4)
#define PL_LINK_WIDTH_SPEED_CONTROL 0x80c
#define PL_SPEED_CHANGE             BIT(17)
#define PL_VERSION_NUMBER           0x8f8
#define PL_VERSION_TYPE             0x8fc
#define PL_ATU_VIEWPORT             0x900
#define PL_LEGACY_END               0x940

#define VERSION_NUMBER              0x3539362a
#define VERSION_TYPE                0x6c753035
#define DEBUG0_LTSSM_L0             0x11

#define ATU_CTRL1                   0x00
#define   ATU_CTRL1_TYPE_MASK       0x1f
#define   ATU_CTRL1_INCREASE_SIZE   BIT(13)
#define ATU_CTRL2                   0x04
#define   ATU_CTRL2_ENABLE          BIT(31)
#define   ATU_CTRL2_BAR_MODE        BIT(30)
#define ATU_LOWER_BASE              0x08
#define ATU_UPPER_BASE              0x0c
#define ATU_LIMIT                   0x10
#define ATU_LOWER_TARGET            0x14
#define ATU_UPPER_TARGET            0x18
#define ATU_UPPER_LIMIT             0x20

#define ATU_TYPE_MEM                0x0
#define ATU_TYPE_IO                 0x2
#define ATU_TYPE_CFG0               0x4
#define ATU_TYPE_CFG1               0x5

enum { MAP_NONE = 0, MAP_MEM, MAP_IO, MAP_CFG };

static Imx95PcieHost *imx95_pcie_root_to_host(Imx95PcieRoot *root)
{
    BusState *bus = qdev_get_parent_bus(DEVICE(root));

    return IMX95_PCIE_HOST(bus->parent);
}

static bool imx95_pcie_atu_range(const Imx95PcieAtu *a, uint64_t *base,
                                    uint64_t *size)
{
    uint64_t limit;

    *base = a->base & ~0xfffULL;
    if (a->ctrl1 & ATU_CTRL1_INCREASE_SIZE) {
        limit = ((uint64_t)a->limit_hi << 32) | (a->limit_lo | 0xfff);
    } else {
        limit = (a->base & ~0xffffffffULL) | (a->limit_lo | 0xfff);
    }
    if (limit < *base) {
        return false;
    }
    *size = limit - *base + 1;
    if (!*size) {
        *size = UINT64_MAX;
    }
    return true;
}

static bool imx95_pcie_map_equal(const Imx95PcieMap *a,
                                    const Imx95PcieMap *b)
{
    return a->kind == b->kind &&
           (a->kind == MAP_NONE ||
            (a->base == b->base && a->size == b->size &&
             a->offset == b->offset));
}

static void imx95_pcie_update_ob(Imx95PcieHost *s, unsigned i)
{
    const Imx95PcieAtu *a = &s->ob[i];
    Imx95PcieOb *r = &s->ob_region[i];
    Imx95PcieMap want = { .kind = MAP_NONE };
    MemoryRegion *cur = NULL;
    uint64_t base, size;

    if ((a->ctrl2 & ATU_CTRL2_ENABLE) &&
        imx95_pcie_atu_range(a, &base, &size)) {
        switch (a->ctrl1 & ATU_CTRL1_TYPE_MASK) {
        case ATU_TYPE_MEM:
            want = (Imx95PcieMap){ MAP_MEM, base, size,
                                     a->target & ~0xfffULL };
            cur = &r->mem;
            break;
        case ATU_TYPE_IO:
            want = (Imx95PcieMap){ MAP_IO, base, size,
                                     a->target & ~0xfffULL };
            cur = &r->io;
            break;
        case ATU_TYPE_CFG0:
        case ATU_TYPE_CFG1:
            want = (Imx95PcieMap){ MAP_CFG, base, size, 0 };
            cur = &r->cfg;
            break;
        default:
            qemu_log_mask(LOG_UNIMP,
                          "imx95.pcie: outbound region %u: TLP type 0x%x "
                          "is not implemented\n", i,
                          a->ctrl1 & ATU_CTRL1_TYPE_MASK);
            break;
        }
    }

    if (imx95_pcie_map_equal(&want, &r->applied)) {
        return;
    }
    r->applied = want;

    memory_region_transaction_begin();
    memory_region_set_enabled(&r->mem, false);
    memory_region_set_enabled(&r->io, false);
    memory_region_set_enabled(&r->cfg, false);
    if (cur) {
        if (cur != &r->cfg) {
            memory_region_set_alias_offset(cur, want.offset);
        }
        memory_region_set_size(cur, want.size);
        memory_region_set_address(cur, want.base);
        memory_region_set_enabled(cur, true);
    }
    memory_region_transaction_commit();
}

static void imx95_pcie_update_ib(Imx95PcieHost *s, unsigned i)
{
    const Imx95PcieAtu *a = &s->ib[i];
    Imx95PcieIb *r = &s->ib_region[i];
    Imx95PcieMap want = { .kind = MAP_NONE };
    uint64_t base, size;

    if ((a->ctrl2 & ATU_CTRL2_ENABLE) &&
        imx95_pcie_atu_range(a, &base, &size)) {
        if ((a->ctrl1 & ATU_CTRL1_TYPE_MASK) == ATU_TYPE_MEM &&
            !(a->ctrl2 & ATU_CTRL2_BAR_MODE)) {
            want = (Imx95PcieMap){ MAP_MEM, base, size,
                                     a->target & ~0xfffULL };
        } else {
            qemu_log_mask(LOG_UNIMP,
                          "imx95.pcie: inbound region %u: only address "
                          "match mode of MEM TLPs is implemented\n", i);
        }
    }

    if (imx95_pcie_map_equal(&want, &r->applied)) {
        return;
    }
    r->applied = want;

    memory_region_transaction_begin();
    memory_region_set_enabled(&r->mem, false);
    if (want.kind) {
        memory_region_set_alias_offset(&r->mem, want.offset);
        memory_region_set_size(&r->mem, want.size);
        memory_region_set_address(&r->mem, want.base);
        memory_region_set_enabled(&r->mem, true);
    }
    memory_region_transaction_commit();
}

static uint64_t imx95_pcie_atu_read(void *opaque, hwaddr off, unsigned size)
{
    Imx95PcieHost *s = opaque;
    unsigned int idx = off >> 9;
    bool ib = (off >> 8) & 1;
    const Imx95PcieAtu *a;

    if (idx >= IMX95_PCIE_NUM_ATU) {
        return 0;
    }
    a = ib ? &s->ib[idx] : &s->ob[idx];

    switch (off & 0xff) {
    case ATU_CTRL1:
        return a->ctrl1;
    case ATU_CTRL2:
        return a->ctrl2;
    case ATU_LOWER_BASE:
        return (uint32_t)a->base;
    case ATU_UPPER_BASE:
        return a->base >> 32;
    case ATU_LIMIT:
        return a->limit_lo | 0xfff;
    case ATU_LOWER_TARGET:
        return (uint32_t)a->target;
    case ATU_UPPER_TARGET:
        return a->target >> 32;
    case ATU_UPPER_LIMIT:
        return a->limit_hi;
    default:
        return 0;
    }
}

static void imx95_pcie_atu_write(void *opaque, hwaddr off, uint64_t val,
                                    unsigned size)
{
    Imx95PcieHost *s = opaque;
    unsigned int idx = off >> 9;
    bool ib = (off >> 8) & 1;
    Imx95PcieAtu *a;

    if (idx >= IMX95_PCIE_NUM_ATU) {
        return;
    }
    a = ib ? &s->ib[idx] : &s->ob[idx];

    switch (off & 0xff) {
    case ATU_CTRL1:
        a->ctrl1 = val;
        break;
    case ATU_CTRL2:
        a->ctrl2 = val;
        break;
    case ATU_LOWER_BASE:
        a->base = deposit64(a->base, 0, 32, val);
        break;
    case ATU_UPPER_BASE:
        a->base = deposit64(a->base, 32, 32, val);
        break;
    case ATU_LIMIT:
        a->limit_lo = val;
        break;
    case ATU_LOWER_TARGET:
        a->target = deposit64(a->target, 0, 32, val);
        break;
    case ATU_UPPER_TARGET:
        a->target = deposit64(a->target, 32, 32, val);
        break;
    case ATU_UPPER_LIMIT:
        a->limit_hi = val;
        break;
    default:
        return;
    }

    if ((off & 0xff) == ATU_CTRL2) {
        if (ib) {
            imx95_pcie_update_ib(s, idx);
        } else {
            imx95_pcie_update_ob(s, idx);
        }
    }
}

static const MemoryRegionOps imx95_pcie_atu_ops = {
    .read = imx95_pcie_atu_read,
    .write = imx95_pcie_atu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl = { .min_access_size = 4, .max_access_size = 4 },
};


static uint64_t imx95_pcie_cfg_access(Imx95PcieOb *r, hwaddr addr,
                                         const uint64_t *val, unsigned len)
{
    Imx95PcieHost *s = r->host;
    const uint64_t target = s->ob[r->index].target;
    const uint8_t busnum = (target >> 24) & 0xff;
    const uint8_t devfn = (target >> 16) & 0xff;
    PCIBus *bus = PCI_HOST_BRIDGE(s)->bus;
    PCIDevice *dev = pci_find_device(bus, busnum, devfn);

    if (!dev) {
        return UINT64_MAX;
    }
    addr &= pci_config_size(dev) - 1;
    if (val) {
        pci_host_config_write_common(dev, addr, pci_config_size(dev), *val,
                                     len);
        return 0;
    }
    return pci_host_config_read_common(dev, addr, pci_config_size(dev), len);
}

static uint64_t imx95_pcie_cfg_read(void *opaque, hwaddr addr, unsigned len)
{
    return imx95_pcie_cfg_access(opaque, addr, NULL, len);
}

static void imx95_pcie_cfg_write(void *opaque, hwaddr addr, uint64_t val,
                                    unsigned len)
{
    imx95_pcie_cfg_access(opaque, addr, &val, len);
}

static const MemoryRegionOps imx95_pcie_cfg_ops = {
    .read = imx95_pcie_cfg_read,
    .write = imx95_pcie_cfg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
};


static void imx95_pcie_count_dev(PCIBus *bus, PCIDevice *dev, void *opaque)
{
    (*(unsigned *)opaque)++;
}

/* Up once LTSSM is enabled and a device is present */
static bool imx95_pcie_link_up(Imx95PcieHost *s)
{
    PCIBus *sec = pci_bridge_get_sec_bus(PCI_BRIDGE(&s->root));
    unsigned int devices = 0;

    if (!s->ltssm_en || !sec) {
        return false;
    }
    pci_for_each_device_under_bus(sec, imx95_pcie_count_dev, &devices);
    return devices > 0;
}

static void imx95_pcie_set_ltssm_en(void *opaque, int n, int level)
{
    Imx95PcieHost *s = opaque;

    s->ltssm_en = level;
}

static uint32_t imx95_pcie_pl_word(Imx95PcieRoot *root, unsigned off)
{
    Imx95PcieHost *host = imx95_pcie_root_to_host(root);
    bool up = imx95_pcie_link_up(host);

    switch (off) {
    case PL_DEBUG0:
        return up ? DEBUG0_LTSSM_L0 : 0;
    case PL_DEBUG1:
        return up ? PL_DEBUG1_LINK_UP : 0;
    case PL_VERSION_NUMBER:
        return VERSION_NUMBER;
    case PL_VERSION_TYPE:
        return VERSION_TYPE;
    case PL_ATU_VIEWPORT:
        /* All-ones: the iATU is unrolled */
        return 0xffffffff;
    default:
        if (off >= PL_BASE && off < PL_ATU_VIEWPORT) {
            return root->pl[(off - PL_BASE) / 4];
        }
        return 0;
    }
}

static uint32_t imx95_pcie_root_config_read(PCIDevice *d, uint32_t address,
                                               int len)
{
    Imx95PcieRoot *root = IMX95_PCIE_ROOT(d);

    if (address >= PL_BASE && address < PL_LEGACY_END) {
        uint32_t word = imx95_pcie_pl_word(root, address & ~3u);
        unsigned int sh = (address & 3) * 8;

        word >>= sh;
        return len >= 4 ? word : word & ((1u << (len * 8)) - 1);
    }
    return pci_default_read_config(d, address, len);
}

static void imx95_pcie_root_config_write(PCIDevice *d, uint32_t address,
                                            uint32_t val, int len)
{
    Imx95PcieRoot *root = IMX95_PCIE_ROOT(d);

    if (address >= PL_BASE && address < PL_LEGACY_END) {
        unsigned int word_off = address & ~3u;
        unsigned int sh = (address & 3) * 8;
        uint32_t mask = (len >= 4 ? ~0u : (1u << (len * 8)) - 1) << sh;
        uint32_t cur, next;

        switch (word_off) {
        case PL_DEBUG0:
        case PL_DEBUG1:
        case PL_VERSION_NUMBER:
        case PL_VERSION_TYPE:
            return;             /* read-only */
        default:
            if (word_off >= PL_ATU_VIEWPORT) {
                return;
            }
        }
        cur = root->pl[(word_off - PL_BASE) / 4];
        next = (cur & ~mask) | ((val << sh) & mask);
        if (word_off == PL_LINK_WIDTH_SPEED_CONTROL) {
            /* Speed change completes at once, the bit self-clears */
            next &= ~PL_SPEED_CHANGE;
        }
        root->pl[(word_off - PL_BASE) / 4] = next;
        return;
    }
    pci_bridge_write_config(d, address, val, len);
}

static uint64_t imx95_pcie_dbi_read(void *opaque, hwaddr addr, unsigned size)
{
    Imx95PcieHost *s = opaque;
    PCIDevice *root = PCI_DEVICE(&s->root);

    if (addr >= pci_config_size(root)) {
        return 0;
    }
    return pci_host_config_read_common(root, addr, pci_config_size(root), size);
}

static void imx95_pcie_dbi_write(void *opaque, hwaddr addr, uint64_t val,
                                    unsigned size)
{
    Imx95PcieHost *s = opaque;
    PCIDevice *root = PCI_DEVICE(&s->root);

    if (addr >= pci_config_size(root)) {
        return;
    }
    pci_host_config_write_common(root, addr, pci_config_size(root), val, size);
}

static const MemoryRegionOps imx95_pcie_dbi_ops = {
    .read = imx95_pcie_dbi_read,
    .write = imx95_pcie_dbi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl = { .min_access_size = 1, .max_access_size = 4 },
};

static void imx95_pcie_root_realize(PCIDevice *dev, Error **errp)
{
    Imx95PcieRoot *root = IMX95_PCIE_ROOT(dev);
    Imx95PcieHost *host = imx95_pcie_root_to_host(root);
    PCIBridge *br = PCI_BRIDGE(dev);

    br->bus_name = host->bus_name;

    pci_set_word(dev->config + PCI_COMMAND,
                 PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    pci_config_set_interrupt_pin(dev->config, 1);
    pci_bridge_initfn(dev, TYPE_PCIE_BUS);

    pcie_port_init_reg(dev);
    pcie_cap_init(dev, 0x70, PCI_EXP_TYPE_ROOT_PORT, 0, &error_fatal);

    msi_nonbroken = true;
    msi_init(dev, 0x50, 32, true, true, &error_fatal);
}

static void imx95_pcie_root_reset(DeviceState *dev)
{
    Imx95PcieRoot *root = IMX95_PCIE_ROOT(dev);

    pci_bridge_reset(dev);
    memset(root->pl, 0, sizeof(root->pl));
}

static void imx95_pcie_root_bus_class_init(ObjectClass *klass,
                                              const void *data)
{
    BusClass *k = BUS_CLASS(klass);

    k->max_dev = 1;
}

static const VMStateDescription vmstate_imx95_pcie_root = {
    .name = "imx95-pcie-root",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBridge),
        VMSTATE_UINT32_ARRAY(pl, Imx95PcieRoot, IMX95_PCIE_PL_WORDS),
        VMSTATE_END_OF_LIST()
    }
};

static void imx95_pcie_root_class_init(ObjectClass *klass, const void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_SYNOPSYS;
    k->device_id = 0xabcd;
    k->revision = 0;
    k->class_id = PCI_CLASS_BRIDGE_PCI;
    k->exit = pci_bridge_exitfn;
    k->realize = imx95_pcie_root_realize;
    k->config_read = imx95_pcie_root_config_read;
    k->config_write = imx95_pcie_root_config_write;

    device_class_set_legacy_reset(dc, imx95_pcie_root_reset);
    dc->user_creatable = false;
    dc->vmsd = &vmstate_imx95_pcie_root;
}

static void imx95_pcie_set_irq(void *opaque, int irq_num, int level)
{
    Imx95PcieHost *s = opaque;

    qemu_set_irq(s->irqs[irq_num], level);
}

static const char *imx95_pcie_root_bus_path(PCIHostState *host_bridge,
                                               PCIBus *rootbus)
{
    Imx95PcieHost *s = IMX95_PCIE_HOST(host_bridge);

    return s->root_path;
}

void imx95_pcie_host_add_dma_window(Imx95PcieHost *s, hwaddr addr,
                                      MemoryRegion *mr, int priority)
{
    memory_region_add_subregion_overlap(&s->address_space_root, addr, mr,
                                        priority);
}

static AddressSpace *imx95_pcie_iommu_as(PCIBus *bus, void *opaque,
                                            int devfn)
{
    Imx95PcieHost *s = opaque;

    return &s->address_space;
}

static const PCIIOMMUOps imx95_pcie_iommu_ops = {
    .get_address_space = imx95_pcie_iommu_as,
};

static void imx95_pcie_invalidate_applied_maps(Imx95PcieHost *s)
{
    unsigned int i;

    for (i = 0; i < IMX95_PCIE_NUM_ATU; i++) {
        s->ob_region[i].applied = (Imx95PcieMap){ .kind = MAP_NONE };
        s->ib_region[i].applied = (Imx95PcieMap){ .kind = MAP_NONE };
    }
}

static void imx95_pcie_update_all(Imx95PcieHost *s)
{
    for (unsigned int i = 0; i < IMX95_PCIE_NUM_ATU; i++) {
        imx95_pcie_update_ob(s, i);
        imx95_pcie_update_ib(s, i);
    }
}

static void imx95_pcie_host_realize(DeviceState *dev, Error **errp)
{
    PCIHostState *pci = PCI_HOST_BRIDGE(dev);
    Imx95PcieHost *s = IMX95_PCIE_HOST(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    MemoryRegion *sysmem = get_system_memory();
    size_t i;

    if (!s->bus_name) {
        s->bus_name = g_strdup("imx95-pcie");
    }
    snprintf(s->root_path, sizeof(s->root_path), "%04x:00", s->domain_nr);

    for (i = 0; i < ARRAY_SIZE(s->irqs); i++) {
        sysbus_init_irq(sbd, &s->irqs[i]);
    }
    sysbus_init_irq(sbd, &s->msi);

    /* mmio 0: dbi, 1: unrolled iATU */
    memory_region_init_io(&s->dbi, OBJECT(s), &imx95_pcie_dbi_ops, s,
                          "imx95-pcie-dbi", 0x10000);
    sysbus_init_mmio(sbd, &s->dbi);
    memory_region_init_io(&s->atu_mmio, OBJECT(s), &imx95_pcie_atu_ops, s,
                          "imx95-pcie-atu", 0x10000);
    sysbus_init_mmio(sbd, &s->atu_mmio);

    memory_region_init(&s->io, OBJECT(s), "imx95-pcie-io",
                       IMX95_PCIE_IO_SIZE);
    memory_region_init(&s->memory, OBJECT(s), "imx95-pcie-mem", UINT64_MAX);

    pci->bus = pci_register_root_bus(dev, "pcie", imx95_pcie_set_irq,
                                     pci_swizzle_map_irq_fn, s, &s->memory,
                                     &s->io, 0, 4,
                                     TYPE_IMX95_PCIE_ROOT_BUS);
    pci->bus->flags |= PCI_BUS_EXTENDED_CONFIG_SPACE;

    memory_region_init(&s->address_space_root, OBJECT(s),
                       "imx95-pcie-dma-root", UINT64_MAX);
    memory_region_add_subregion(&s->address_space_root, 0x0, &s->memory);
    memory_region_init_alias(&s->inbound, OBJECT(s), "imx95-pcie-inbound",
                             sysmem, 0, UINT64_MAX);
    memory_region_add_subregion_overlap(&s->address_space_root, 0x0,
                                        &s->inbound, -1);
    address_space_init(&s->address_space, &s->address_space_root,
                       "imx95-pcie-dma");
    pci_setup_iommu(pci->bus, &imx95_pcie_iommu_ops, s);

    for (i = 0; i < IMX95_PCIE_NUM_ATU; i++) {
        Imx95PcieOb *o = &s->ob_region[i];
        Imx95PcieIb *b = &s->ib_region[i];
        g_autofree char *name = NULL;

        o->host = s;
        o->index = i;

        name = g_strdup_printf("imx95-pcie-ob%zu-mem", i);
        memory_region_init_alias(&o->mem, OBJECT(s), name, &s->memory, 0, 4);
        memory_region_add_subregion(sysmem, 0, &o->mem);
        memory_region_set_enabled(&o->mem, false);
        g_free(g_steal_pointer(&name));

        name = g_strdup_printf("imx95-pcie-ob%zu-io", i);
        memory_region_init_alias(&o->io, OBJECT(s), name, &s->io, 0, 4);
        memory_region_add_subregion(sysmem, 0, &o->io);
        memory_region_set_enabled(&o->io, false);
        g_free(g_steal_pointer(&name));

        name = g_strdup_printf("imx95-pcie-ob%zu-cfg", i);
        memory_region_init_io(&o->cfg, OBJECT(s), &imx95_pcie_cfg_ops, o,
                              name, 4);
        memory_region_add_subregion(sysmem, 0, &o->cfg);
        memory_region_set_enabled(&o->cfg, false);
        g_free(g_steal_pointer(&name));

        name = g_strdup_printf("imx95-pcie-ib%zu", i);
        memory_region_init_alias(&b->mem, OBJECT(s), name, sysmem, 0, 4);
        memory_region_add_subregion_overlap(&s->address_space_root, 0,
                                            &b->mem, 1);
        memory_region_set_enabled(&b->mem, false);
    }

    if (!qdev_realize(DEVICE(&s->root), BUS(pci->bus), errp)) {
        return;
    }
}

static void imx95_pcie_host_reset(DeviceState *dev)
{
    Imx95PcieHost *s = IMX95_PCIE_HOST(dev);

    memset(s->ob, 0, sizeof(s->ob));
    memset(s->ib, 0, sizeof(s->ib));
    s->ltssm_en = false;
    imx95_pcie_update_all(s);
}

static int imx95_pcie_host_post_load(void *opaque, int version_id)
{
    Imx95PcieHost *s = opaque;

    imx95_pcie_invalidate_applied_maps(s);
    imx95_pcie_update_all(s);
    return 0;
}

static const VMStateDescription vmstate_imx95_pcie_atu = {
    .name = "imx95-pcie-atu",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(ctrl1, Imx95PcieAtu),
        VMSTATE_UINT32(ctrl2, Imx95PcieAtu),
        VMSTATE_UINT64(base, Imx95PcieAtu),
        VMSTATE_UINT32(limit_lo, Imx95PcieAtu),
        VMSTATE_UINT32(limit_hi, Imx95PcieAtu),
        VMSTATE_UINT64(target, Imx95PcieAtu),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_imx95_pcie_host = {
    .name = "imx95-pcie-host",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = imx95_pcie_host_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(ob, Imx95PcieHost, IMX95_PCIE_NUM_ATU, 1,
                             vmstate_imx95_pcie_atu, Imx95PcieAtu),
        VMSTATE_STRUCT_ARRAY(ib, Imx95PcieHost, IMX95_PCIE_NUM_ATU, 1,
                             vmstate_imx95_pcie_atu, Imx95PcieAtu),
        VMSTATE_BOOL(ltssm_en, Imx95PcieHost),
        VMSTATE_END_OF_LIST()
    }
};

static const Property imx95_pcie_host_props[] = {
    DEFINE_PROP_STRING("bus-name", Imx95PcieHost, bus_name),
    DEFINE_PROP_UINT32("domain-nr", Imx95PcieHost, domain_nr, 0),
};

static void imx95_pcie_host_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIHostBridgeClass *hc = PCI_HOST_BRIDGE_CLASS(klass);

    hc->root_bus_path = imx95_pcie_root_bus_path;
    dc->realize = imx95_pcie_host_realize;
    dc->fw_name = "pci";
    dc->vmsd = &vmstate_imx95_pcie_host;
    device_class_set_props(dc, imx95_pcie_host_props);
    device_class_set_legacy_reset(dc, imx95_pcie_host_reset);
}

static void imx95_pcie_host_init(Object *obj)
{
    Imx95PcieHost *s = IMX95_PCIE_HOST(obj);

    object_initialize_child(obj, "root", &s->root, TYPE_IMX95_PCIE_ROOT);
    qdev_init_gpio_in_named(DEVICE(obj), imx95_pcie_set_ltssm_en, "ltssm-en",
                            1);
    qdev_prop_set_int32(DEVICE(&s->root), "addr", PCI_DEVFN(0, 0));
    qdev_prop_set_bit(DEVICE(&s->root), "multifunction", false);
}

static const TypeInfo imx95_pcie_types[] = {
    {
        .name           = TYPE_IMX95_PCIE_ROOT_BUS,
        .parent         = TYPE_PCIE_BUS,
        .instance_size  = sizeof(Imx95PcieRootBus),
        .class_init     = imx95_pcie_root_bus_class_init,
    }, {
        .name           = TYPE_IMX95_PCIE_HOST,
        .parent         = TYPE_PCI_HOST_BRIDGE,
        .instance_size  = sizeof(Imx95PcieHost),
        .instance_init  = imx95_pcie_host_init,
        .class_init     = imx95_pcie_host_class_init,
    }, {
        .name           = TYPE_IMX95_PCIE_ROOT,
        .parent         = TYPE_PCI_BRIDGE,
        .instance_size  = sizeof(Imx95PcieRoot),
        .class_init     = imx95_pcie_root_class_init,
        .interfaces     = (const InterfaceInfo[]) {
            { INTERFACE_PCIE_DEVICE },
            { }
        },
    },
};

DEFINE_TYPES(imx95_pcie_types)

