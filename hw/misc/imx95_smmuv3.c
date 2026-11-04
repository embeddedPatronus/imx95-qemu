/*
 * NXP i.MX95 smmuv3
 *
 * Minimal SMMUv3 front end: the global register aperture with GBPA and its
 * UPDATE handshake, and the performance monitor counter group pages.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "hw/core/qdev-properties.h"
#include "hw/misc/imx95_smmuv3.h"
#include "migration/vmstate.h"
#include "hw/core/irq.h"
#include "system/memory.h"

#define IMX95_SMMUV3_GBPA 0x0044

#define IMX95_SMMUV3_GBPA_UPDATE BIT(31)
#define IMX95_SMMUV3_GBPA_ABORT BIT(20)
#define IMX95_SMMUV3_GBPA_SHCFG_MASK MAKE_64BIT_MASK(12, 2)

#define IMX95_SMMUV3_GBPA_WRITABLE_MASK \
    (IMX95_SMMUV3_GBPA_ABORT | IMX95_SMMUV3_GBPA_SHCFG_MASK)


static uint64_t imx95_smmuv3_global_read(void *opaque,
                                         hwaddr offset,
                                         unsigned size)
{
    Imx95Smmuv3State *s = opaque;

    switch (offset) {
    case IMX95_SMMUV3_GBPA:
        return s->gbpa & ~IMX95_SMMUV3_GBPA_UPDATE;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "imx95.smmuv3: unimplemented read "
                      "offset=0x%04" HWADDR_PRIx
                      " size=%u\n",
                      offset,
                      size);
        return 0;
    }
}

static void imx95_smmuv3_global_write(void *opaque,
                                      hwaddr offset,
                                      uint64_t value,
                                      unsigned size)
{
    Imx95Smmuv3State *s = opaque;
    uint32_t requested = value;

    switch (offset) {
    case IMX95_SMMUV3_GBPA:
        s->gbpa = requested & IMX95_SMMUV3_GBPA_WRITABLE_MASK;

        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.smmuv3: GBPA write "
                      "requested=0x%08x committed=0x%08x\n",
                      requested,
                      s->gbpa);
        return;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "imx95.smmuv3: unimplemented write "
                      "offset=0x%04" HWADDR_PRIx
                      " size=%u value=0x%08x\n",
                      offset,
                      size,
                      requested);
        return;
    }
}

static const MemoryRegionOps imx95_smmuv3_global_ops = {
    .read = imx95_smmuv3_global_read,
    .write = imx95_smmuv3_global_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void imx95_smmuv3_reset(DeviceState *dev)
{
    Imx95Smmuv3State *s = IMX95_SMMUV3(dev);
    s->gbpa = 0;


    s->gbpa = 0;
}

static void imx95_smmuv3_init(Object *obj)
{
    Imx95Smmuv3State *s = IMX95_SMMUV3(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_smmuv3_global_ops, s,
                          TYPE_IMX95_SMMUV3, IMX95_SMMUV3_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription vmstate_imx95_smmuv3 = {
    .name = TYPE_IMX95_SMMUV3,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(gbpa, Imx95Smmuv3State),
        VMSTATE_END_OF_LIST()
    }
};

static void imx95_smmuv3_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, imx95_smmuv3_reset);
    dc->vmsd = &vmstate_imx95_smmuv3;
    dc->desc = "i.MX95 smmuv3";
}


/* ------------------------------------------------------------------------ */
/* SMMUv3 PMCG                                                              */
/* ------------------------------------------------------------------------ */

#define PMCG_EVCNTR0        0x000
#define PMCG_EVTYPER0       0x400
#define PMCG_SMR0           0xA00
#define PMCG_CNTENSET0      0xC00
#define PMCG_CNTENCLR0      0xC20
#define PMCG_INTENSET0      0xC40
#define PMCG_INTENCLR0      0xC60
#define PMCG_OVSCLR0        0xC80
#define PMCG_OVSSET0        0xCC0
#define PMCG_CFGR           0xE00
#define PMCG_CR             0xE04
#define PMCG_IIDR           0xE08
#define PMCG_CEID0          0xE20
#define PMCG_CEID1          0xE28
#define PMCG_IRQ_CTRL       0xE50
#define PMCG_IRQ_CFG0       0xE58
#define PMCG_IRQ_CFG1       0xE60
#define PMCG_IRQ_CFG2       0xE64

#define PMCG_CFGR_VALUE \
    (((IMX95_SMMU_PMCG_NUM_CTRS - 1) << 0) | (31 << 8))

static uint64_t imx95_smmu_pmcg_read(void *opaque, hwaddr offset,
                                     unsigned size)
{
    Imx95SmmuPmcgState *s = opaque;
    unsigned n;

    if (offset < PMCG_EVCNTR0 + 4 * IMX95_SMMU_PMCG_NUM_CTRS) {
        return s->evcntr[offset / 4];
    }
    if (offset >= PMCG_EVTYPER0 &&
        offset < PMCG_EVTYPER0 + 4 * IMX95_SMMU_PMCG_NUM_CTRS) {
        n = (offset - PMCG_EVTYPER0) / 4;
        return s->evtyper[n];
    }
    if (offset >= PMCG_SMR0 &&
        offset < PMCG_SMR0 + 4 * IMX95_SMMU_PMCG_NUM_CTRS) {
        n = (offset - PMCG_SMR0) / 4;
        return s->smr[n];
    }

    switch (offset) {
    case PMCG_CNTENSET0:
    case PMCG_CNTENCLR0:
        return s->cnten;
    case PMCG_INTENSET0:
    case PMCG_INTENCLR0:
        return s->inten;
    case PMCG_OVSCLR0:
    case PMCG_OVSSET0:
        return s->ovs;
    case PMCG_CFGR:
        return PMCG_CFGR_VALUE;
    case PMCG_CR:
        return s->cr;
    case PMCG_IIDR:
    case PMCG_CEID0:
    case PMCG_CEID0 + 4:
    case PMCG_CEID1:
    case PMCG_CEID1 + 4:
        return 0;
    case PMCG_IRQ_CTRL:
        return s->irq_ctrl;
    case PMCG_IRQ_CFG0:
        return s->irq_cfg0[0];
    case PMCG_IRQ_CFG0 + 4:
        return s->irq_cfg0[1];
    case PMCG_IRQ_CFG1:
        return s->irq_cfg1;
    case PMCG_IRQ_CFG2:
        return s->irq_cfg2;
    default:
        /* ID registers and reserved space read as zero */
        return 0;
    }
}

static void imx95_smmu_pmcg_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned size)
{
    Imx95SmmuPmcgState *s = opaque;
    uint32_t v = value;
    uint32_t ctr_mask = MAKE_64BIT_MASK(0, IMX95_SMMU_PMCG_NUM_CTRS);
    unsigned n;

    if (offset < PMCG_EVCNTR0 + 4 * IMX95_SMMU_PMCG_NUM_CTRS) {
        s->evcntr[offset / 4] = v;
        return;
    }
    if (offset >= PMCG_EVTYPER0 &&
        offset < PMCG_EVTYPER0 + 4 * IMX95_SMMU_PMCG_NUM_CTRS) {
        n = (offset - PMCG_EVTYPER0) / 4;
        s->evtyper[n] = v;
        return;
    }
    if (offset >= PMCG_SMR0 &&
        offset < PMCG_SMR0 + 4 * IMX95_SMMU_PMCG_NUM_CTRS) {
        n = (offset - PMCG_SMR0) / 4;
        s->smr[n] = v;
        return;
    }

    switch (offset) {
    case PMCG_CNTENSET0:
        s->cnten |= v & ctr_mask;
        break;
    case PMCG_CNTENCLR0:
        s->cnten &= ~v;
        break;
    case PMCG_INTENSET0:
        s->inten |= v & ctr_mask;
        break;
    case PMCG_INTENCLR0:
        s->inten &= ~v;
        break;
    case PMCG_OVSCLR0:
        s->ovs &= ~v;
        break;
    case PMCG_OVSSET0:
        s->ovs |= v & ctr_mask;
        break;
    case PMCG_CR:
        s->cr = v & 1;
        break;
    case PMCG_IRQ_CTRL:
        s->irq_ctrl = v & 1;
        break;
    case PMCG_IRQ_CFG0:
        s->irq_cfg0[0] = v;
        break;
    case PMCG_IRQ_CFG0 + 4:
        s->irq_cfg0[1] = v;
        break;
    case PMCG_IRQ_CFG1:
        s->irq_cfg1 = v;
        break;
    case PMCG_IRQ_CFG2:
        s->irq_cfg2 = v;
        break;
    default:
        /* CFGR, IIDR, CEID and the ID registers are read-only */
        break;
    }
}

static const MemoryRegionOps imx95_smmu_pmcg_ops = {
    .read = imx95_smmu_pmcg_read,
    .write = imx95_smmu_pmcg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 8 },
    .impl = { .min_access_size = 4, .max_access_size = 4 },
};

static void imx95_smmu_pmcg_reset(DeviceState *dev)
{
    Imx95SmmuPmcgState *s = IMX95_SMMU_PMCG(dev);

    memset(s->evcntr, 0, sizeof(s->evcntr));
    memset(s->evtyper, 0, sizeof(s->evtyper));
    memset(s->smr, 0, sizeof(s->smr));
    s->cnten = s->inten = s->ovs = s->cr = s->irq_ctrl = 0;
    s->irq_cfg0[0] = s->irq_cfg0[1] = s->irq_cfg1 = s->irq_cfg2 = 0;
}

static void imx95_smmu_pmcg_init(Object *obj)
{
    Imx95SmmuPmcgState *s = IMX95_SMMU_PMCG(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_smmu_pmcg_ops, s,
                          TYPE_IMX95_SMMU_PMCG, IMX95_SMMU_PMCG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription vmstate_imx95_smmu_pmcg = {
    .name = TYPE_IMX95_SMMU_PMCG,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(evcntr, Imx95SmmuPmcgState,
                             IMX95_SMMU_PMCG_NUM_CTRS),
        VMSTATE_UINT32_ARRAY(evtyper, Imx95SmmuPmcgState,
                             IMX95_SMMU_PMCG_NUM_CTRS),
        VMSTATE_UINT32_ARRAY(smr, Imx95SmmuPmcgState,
                             IMX95_SMMU_PMCG_NUM_CTRS),
        VMSTATE_UINT32(cnten, Imx95SmmuPmcgState),
        VMSTATE_UINT32(inten, Imx95SmmuPmcgState),
        VMSTATE_UINT32(ovs, Imx95SmmuPmcgState),
        VMSTATE_UINT32(cr, Imx95SmmuPmcgState),
        VMSTATE_UINT32(irq_ctrl, Imx95SmmuPmcgState),
        VMSTATE_UINT32_ARRAY(irq_cfg0, Imx95SmmuPmcgState, 2),
        VMSTATE_UINT32(irq_cfg1, Imx95SmmuPmcgState),
        VMSTATE_UINT32(irq_cfg2, Imx95SmmuPmcgState),
        VMSTATE_END_OF_LIST()
    }
};

static void imx95_smmu_pmcg_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, imx95_smmu_pmcg_reset);
    dc->vmsd = &vmstate_imx95_smmu_pmcg;
    dc->desc = "i.MX95 SMMUv3 PMCG";
}

static const TypeInfo imx95_smmuv3_types[] = {
    {
        .name = TYPE_IMX95_SMMUV3,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Imx95Smmuv3State),
        .instance_init = imx95_smmuv3_init,
        .class_init = imx95_smmuv3_class_init,
    },
    {
        .name = TYPE_IMX95_SMMU_PMCG,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Imx95SmmuPmcgState),
        .instance_init = imx95_smmu_pmcg_init,
        .class_init = imx95_smmu_pmcg_class_init,
    },
};

DEFINE_TYPES(imx95_smmuv3_types)
