/*
 * NXP i.MX95 Messaging Unit
 *
 * Hardware-only model. This device implements the MU register interface,
 * peer transfer, reset state, and aggregated interrupt outputs. It does not
 * interpret SCMI, ELE, RPMsg, or any other mailbox protocol.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/core/irq.h"
#include "hw/misc/imx95_mu.h"
#include "qemu/timer.h"
#include "trace.h"
#include "hw/core/resettable.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define MU_VER             0x000
#define MU_PAR             0x004
#define MU_CR              0x008
#define MU_SR              0x00c
#define MU_CCR0            0x010
#define MU_CIER0           0x014
#define MU_CSSR0           0x018
#define MU_CSR0            0x01c
#define MU_FCR             0x100
#define MU_FSR             0x104
#define MU_GIER            0x110
#define MU_GCR             0x114
#define MU_GSR             0x118
#define MU_TCR             0x120
#define MU_TSR             0x124
#define MU_RCR             0x128
#define MU_RSR             0x12c
#define MU_TR_BASE         0x200
#define MU_RR_BASE         0x280

#define MU_VER_RESET       0x0406000fU
#define MU_PAR_RESET       0x03040404U
#define MU_CCR0_RESET      0x00000014U
#define MU_CHANNEL_MASK    0x0000000fU
#define MU_CR_MURIE        BIT(1)
#define MU_CR_MUR          BIT(0)

#define MU_SR_RFP          BIT(6)
#define MU_SR_TEP          BIT(5)
#define MU_SR_GIRP         BIT(4)
#define MU_SR_MURIP        BIT(1)
#define MU_SR_MURS         BIT(0)

#define MU_CIER0_MASK      0x000000a4U
#define MU_CSSR0_W1C_MASK  0x000000a4U

static inline unsigned mu_peer(unsigned side)
{
    return side ^ 1U;
}

static uint32_t mu_sr(IMX95MUState *s, unsigned side)
{
    uint32_t value = 0;

    if (s->rx_full[side]) {
        value |= MU_SR_RFP;
    }
    if (s->tx_empty[side] != 0) {
        value |= MU_SR_TEP;
    }
    if (s->gp_pending[mu_peer(side)] != 0) {
        value |= MU_SR_GIRP;
    }
    value |= s->cr[side] & MU_SR_MURIP;
    return value;
}

static void mu_update_irq(IMX95MUState *s, unsigned side)
{
    bool pending;

    pending = ((s->tcr[side] & s->tx_empty[side]) != 0) ||
              ((s->rcr[side] & s->rx_full[side]) != 0) ||
              ((s->gier[side] & s->gp_pending[side]) != 0) ||
              ((s->cier0[side] & s->cssr0[side]) != 0) ||
              ((s->cr[side] & MU_CR_MURIE) != 0 &&
               (mu_sr(s, side) & MU_SR_MURIP) != 0);
    if (pending != s->irq_level[side]) {
        s->irq_level[side] = pending;
        trace_imx95_mu_irq(object_get_canonical_path_component(OBJECT(s)),
                           side, pending,
                           qemu_clock_get_us(QEMU_CLOCK_VIRTUAL));
    }

    qemu_set_irq(s->irq[side], pending);
}

static void mu_update_irqs(IMX95MUState *s)
{
    mu_update_irq(s, 0);
    mu_update_irq(s, 1);
}

static void mu_reset_state(IMX95MUState *s)
{
    unsigned side;

    memset(s->cr, 0, sizeof(s->cr));
    memset(s->cier0, 0, sizeof(s->cier0));
    memset(s->cssr0, 0, sizeof(s->cssr0));
    memset(s->fcr, 0, sizeof(s->fcr));
    memset(s->gier, 0, sizeof(s->gier));
    memset(s->gp_pending, 0, sizeof(s->gp_pending));
    memset(s->tcr, 0, sizeof(s->tcr));
    memset(s->rcr, 0, sizeof(s->rcr));
    memset(s->tx, 0, sizeof(s->tx));
    memset(s->rx, 0, sizeof(s->rx));
    s->flags = 0;

    for (side = 0; side < 2; side++) {
        s->ccr0[side] = MU_CCR0_RESET;
        s->tx_empty[side] = MU_CHANNEL_MASK;
        s->rx_full[side] = 0;
    }
    mu_update_irqs(s);
}

typedef struct IMX95MUEndpoint {
    IMX95MUState *mu;
    unsigned side;
} IMX95MUEndpoint;

static uint64_t mu_endpoint_read(void *opaque, hwaddr offset, unsigned size)
{
    IMX95MUEndpoint *ep = opaque;
    IMX95MUState *s = ep->mu;
    unsigned side = ep->side;
    unsigned peer = mu_peer(side);
    unsigned index;
    uint32_t value;

    switch (offset) {
    case MU_VER:   return MU_VER_RESET;
    case MU_PAR:   return MU_PAR_RESET;
    case MU_CR:    return s->cr[side] & MU_CR_MURIE;
    case MU_SR:    return mu_sr(s, side);
    case MU_CCR0:  return s->ccr0[side];
    case MU_CIER0: return s->cier0[side];
    case MU_CSSR0: return s->cssr0[side];
    case MU_CSR0:  return 0;
    case MU_FCR:   return s->fcr[side];
    case MU_FSR:   return s->flags;
    case MU_GIER:  return s->gier[side];
    case MU_GCR:   return s->gp_pending[peer];
    case MU_GSR:   return s->gp_pending[side];
    case MU_TCR:   return s->tcr[side];
    case MU_TSR:   return s->tx_empty[side];
    case MU_RCR:   return s->rcr[side];
    case MU_RSR:   return s->rx_full[side];
    default:
        break;
    }

    if (offset >= MU_TR_BASE && offset < MU_TR_BASE + 0x10) {
        index = (offset - MU_TR_BASE) >> 2;
        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95-mu: side %u read from write-only TR%u\n",
                      side, index);
        return 0;
    }

    if (offset >= MU_RR_BASE && offset < MU_RR_BASE + 0x10) {
        index = (offset - MU_RR_BASE) >> 2;
        value = s->rx[side][index];
        if (s->rx_full[side] & BIT(index)) {
            s->rx_full[side] &= ~BIT(index);
            s->tx_empty[peer] |= BIT(index);
            mu_update_irqs(s);
        }
        return value;
    }

    qemu_log_mask(LOG_UNIMP,
                  "imx95-mu: side %u unimplemented read offset 0x%03" HWADDR_PRIx "\n",
                  side, offset);
    return 0;
}

static void mu_endpoint_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned size)
{
    IMX95MUEndpoint *ep = opaque;
    IMX95MUState *s = ep->mu;
    unsigned side = ep->side;
    unsigned peer = mu_peer(side);
    unsigned index;
    uint32_t v = value;

    switch (offset) {
    case MU_CR:
        s->cr[side] = v & MU_CR_MURIE;
        if (v & MU_CR_MUR) {
            mu_reset_state(s);
            s->cssr0[peer] |= MU_SR_MURIP;
            mu_update_irqs(s);
        }
        return;
    case MU_CCR0:
        s->ccr0[side] = v;
        mu_update_irqs(s);
        return;
    case MU_CIER0:
        s->cier0[side] = v & MU_CIER0_MASK;
        mu_update_irq(s, side);
        return;
    case MU_CSSR0:
        s->cssr0[side] &= ~(v & MU_CSSR0_W1C_MASK);
        mu_update_irq(s, side);
        return;
    case MU_FCR:
        s->fcr[side] = v;
        s->flags = v;
        return;
    case MU_GIER:
        s->gier[side] = v & MU_CHANNEL_MASK;
        mu_update_irq(s, side);
        return;
    case MU_GCR:
        s->gp_pending[peer] |= v & MU_CHANNEL_MASK;
        trace_imx95_mu_gcr_write(object_get_canonical_path_component(OBJECT(s)),
                                 side, v & MU_CHANNEL_MASK,
                                 qemu_clock_get_us(QEMU_CLOCK_VIRTUAL));
        mu_update_irqs(s);
        return;
    case MU_GSR:
        trace_imx95_mu_gsr_clear(object_get_canonical_path_component(OBJECT(s)),
                                 side, v & MU_CHANNEL_MASK,
                                 s->gp_pending[side] & ~v & MU_CHANNEL_MASK,
                                 qemu_clock_get_us(QEMU_CLOCK_VIRTUAL));
        s->gp_pending[side] &= ~(v & MU_CHANNEL_MASK);
        mu_update_irqs(s);
        return;
    case MU_TCR:
        s->tcr[side] = v & MU_CHANNEL_MASK;
        mu_update_irq(s, side);
        return;
    case MU_RCR:
        s->rcr[side] = v & MU_CHANNEL_MASK;
        mu_update_irq(s, side);
        return;
    case MU_VER:
    case MU_PAR:
    case MU_SR:
    case MU_CSR0:
    case MU_FSR:
    case MU_TSR:
    case MU_RSR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95-mu: side %u write to read-only offset 0x%03" HWADDR_PRIx "\n",
                      side, offset);
        return;
    default:
        break;
    }

    if (offset >= MU_TR_BASE && offset < MU_TR_BASE + 0x10) {
        index = (offset - MU_TR_BASE) >> 2;
        if (!(s->tx_empty[side] & BIT(index))) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "imx95-mu: side %u write to full TR%u ignored\n",
                          side, index);
            return;
        }
        s->tx[side][index] = v;
        s->rx[peer][index] = v;
        s->tx_empty[side] &= ~BIT(index);
        s->rx_full[peer] |= BIT(index);
        mu_update_irqs(s);
        return;
    }

    if (offset >= MU_RR_BASE && offset < MU_RR_BASE + 0x10) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95-mu: side %u write to read-only RR%u ignored\n",
                      side, (unsigned)((offset - MU_RR_BASE) >> 2));
        return;
    }

    qemu_log_mask(LOG_UNIMP,
                  "imx95-mu: side %u unimplemented write offset 0x%03" HWADDR_PRIx
                  " value 0x%08x\n", side, offset, v);
}

static const MemoryRegionOps mu_ops = {
    .read = mu_endpoint_read,
    .write = mu_endpoint_write,
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

static ResettablePhases mu_parent_phases;

static void mu_reset_enter(Object *obj, ResetType type)
{
    if (mu_parent_phases.enter) {
        mu_parent_phases.enter(obj, type);
    }
    mu_reset_state(IMX95_MU(obj));
}

static int mu_post_load(void *opaque, int version_id)
{
    mu_update_irqs(opaque);
    return 0;
}

static const VMStateDescription vmstate_imx95_mu = {
    .name = TYPE_IMX95_MU,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = mu_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(cr, IMX95MUState, 2),
        VMSTATE_UINT32_ARRAY(ccr0, IMX95MUState, 2),
        VMSTATE_UINT32_ARRAY(cier0, IMX95MUState, 2),
        VMSTATE_UINT32_ARRAY(cssr0, IMX95MUState, 2),
        VMSTATE_UINT32_ARRAY(fcr, IMX95MUState, 2),
        VMSTATE_UINT32(flags, IMX95MUState),
        VMSTATE_UINT32_ARRAY(gier, IMX95MUState, 2),
        VMSTATE_UINT32_ARRAY(gp_pending, IMX95MUState, 2),
        VMSTATE_UINT32_ARRAY(tcr, IMX95MUState, 2),
        VMSTATE_UINT32_ARRAY(rcr, IMX95MUState, 2),
        VMSTATE_UINT32_2DARRAY(tx, IMX95MUState, 2, IMX95_MU_NUM_CHANNELS),
        VMSTATE_UINT32_2DARRAY(rx, IMX95MUState, 2, IMX95_MU_NUM_CHANNELS),
        VMSTATE_UINT8_ARRAY(tx_empty, IMX95MUState, 2),
        VMSTATE_UINT8_ARRAY(rx_full, IMX95MUState, 2),
        VMSTATE_END_OF_LIST()
    },
};

static void mu_init(Object *obj)
{
    IMX95MUState *s = IMX95_MU(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    IMX95MUEndpoint *ep;
    unsigned side;

    for (side = 0; side < 2; side++) {
        ep = g_new0(IMX95MUEndpoint, 1);
        ep->mu = s;
        ep->side = side;
        memory_region_init_io(&s->iomem[side], obj, &mu_ops, ep,
                              side ? "imx95-mu.mub" : "imx95-mu.mua",
                              IMX95_MU_MMIO_SIZE);
        sysbus_init_mmio(sbd, &s->iomem[side]);
        sysbus_init_irq(sbd, &s->irq[side]);
    }
}

static void mu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->desc = "NXP i.MX95 Messaging Unit";
    dc->vmsd = &vmstate_imx95_mu;
    resettable_class_set_parent_phases(rc, mu_reset_enter, NULL, NULL,
                                       &mu_parent_phases);
}

static const TypeInfo mu_type_info = {
    .name = TYPE_IMX95_MU,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMX95MUState),
    .instance_init = mu_init,
    .class_init = mu_class_init,
};

static void mu_register_types(void)
{
    type_register_static(&mu_type_info);
}

type_init(mu_register_types)
