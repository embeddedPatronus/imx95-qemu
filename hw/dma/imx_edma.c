/*
 * NXP i.MX enhanced Direct Memory Access controller
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/bswap.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/dma/imx_edma.h"
#include "migration/vmstate.h"

/* Management page */
#define MP_CSR          0x000
#define MP_ES           0x004
#define MP_INT_LOW      0x008
#define MP_INT_HIGH     0x00c
#define MP_HRS_LOW      0x010
#define MP_HRS_HIGH     0x014
#define MP_STOPCH       0x020
#define MP_SSR_LOW      0x030
#define MP_SSR_HIGH     0x034
#define MP_GRPRI        0x100
#define MP_MUX          0x200
#define MP_PROT         0x400

#define MP_CSR_EDBG     BIT(1)
#define MP_CSR_ERCA     BIT(2)
#define MP_CSR_GCPC     BIT(3)
#define MP_CSR_HAE      BIT(4)
#define MP_CSR_HALT     BIT(5)
#define MP_CSR_GCLC     BIT(6)
#define MP_CSR_GMRC     BIT(7)
#define MP_CSR_VER_SHIFT 16

#define MP_ES_VLD       BIT(31)

/* Channel registers, relative to the channel page */
#define CH_CSR          0x00
#define CH_ES           0x04
#define CH_INT          0x08
#define CH_SBR          0x0c
#define CH_PRI          0x10
#define CH_MATTR        0x18
#define CH_TCD          0x20

#define CH_CSR_ERQ      BIT(0)
#define CH_CSR_EARQ     BIT(1)
#define CH_CSR_EEI      BIT(2)
#define CH_CSR_CX       BIT(6)
#define CH_CSR_ECX      BIT(7)
#define CH_CSR_DONE     BIT(30)
#define CH_CSR_ACTIVE   BIT(31)

#define ES_DBE          BIT(0)
#define ES_SBE          BIT(1)
#define ES_SGE          BIT(2)
#define ES_NCE          BIT(3)
#define ES_DOE          BIT(4)
#define ES_DAE          BIT(5)
#define ES_SOE          BIT(6)
#define ES_SAE          BIT(7)
#define ES_ECX          BIT(8)
#define ES_ERR          BIT(31)

/* TCD control and status */
#define TCD_CSR_START       BIT(0)
#define TCD_CSR_INTMAJOR    BIT(1)
#define TCD_CSR_INTHALF     BIT(2)
#define TCD_CSR_DREQ        BIT(3)
#define TCD_CSR_ESG         BIT(4)
#define TCD_CSR_MAJORELINK  BIT(5)
#define TCD_CSR_ESDA        BIT(7)
#define TCD_CSR_TMC_SHIFT   14      /* eDMA5: 0 R/W, 1 read only, 2 write only */

#define TCD_NBYTES_DMLOE    BIT(30)
#define TCD_NBYTES_SMLOE    BIT(31)

#define PRI_APL_MASK        7

struct IMXEDMATcdLayout {
    uint8_t soff, attr, nbytes, slast, daddr, dlast, doff, citer, csr, biter;
    uint8_t size;
    bool a64;
};

/* The fields start at offset 0x20 of the channel page; saddr is at 0 */
static const struct IMXEDMATcdLayout tcd_v3 = {
    .soff = 0x04, .attr = 0x06, .nbytes = 0x08, .slast = 0x0c,
    .daddr = 0x10, .dlast = 0x18, .doff = 0x14, .citer = 0x16,
    .csr = 0x1c, .biter = 0x1e, .size = 32, .a64 = false,
};

static const struct IMXEDMATcdLayout tcd_v5 = {
    .soff = 0x08, .attr = 0x0a, .nbytes = 0x0c, .slast = 0x10,
    .daddr = 0x18, .dlast = 0x20, .doff = 0x28, .citer = 0x2a,
    .csr = 0x2c, .biter = 0x2e, .size = 64, .a64 = true,
};

/* Transfer sizes of TCD ATTR[SSIZE] and ATTR[DSIZE] */
static const unsigned int xfer_size[8] = { 1, 2, 4, 8, 16, 32, 64, 0 };

static uint8_t *edma_tcd(IMXEDMAState *s, unsigned int ch)
{
    return &s->tcd[ch * IMX_EDMA_TCD_BYTES];
}

static uint64_t tcd_get_addr(const struct IMXEDMATcdLayout *l,
                             const uint8_t *t, unsigned int off)
{
    return l->a64 ? ldq_le_p(t + off) : (uint32_t)ldl_le_p(t + off);
}

static void tcd_set_addr(const struct IMXEDMATcdLayout *l, uint8_t *t,
                         unsigned int off, uint64_t v)
{
    if (l->a64) {
        stq_le_p(t + off, v);
    } else {
        stl_le_p(t + off, v);
    }
}

static uint64_t tcd_get_adj(const struct IMXEDMATcdLayout *l,
                            const uint8_t *t, unsigned int off)
{
    return l->a64 ? ldq_le_p(t + off) : (uint64_t)(int32_t)ldl_le_p(t + off);
}

static uint64_t edma_addr(const struct IMXEDMATcdLayout *l, uint64_t a)
{
    return l->a64 ? a : (uint32_t)a;
}

static uint64_t edma_next_addr(const struct IMXEDMATcdLayout *l,
                               uint64_t addr, int64_t off,
                               unsigned int mod)
{
    uint64_t next = addr + off;

    if (mod) {
        uint64_t m = (1ULL << mod) - 1;

        next = (addr & ~m) | (next & m);
    }
    return edma_addr(l, next);
}

static void edma_update_ch_irq(IMXEDMAState *s, unsigned int ch)
{
    if (s->paired_irqs) {
        unsigned int first = ch & ~1u;
        bool level = s->ch_int[first] & 1;

        if (first + 1 < s->num_channels) {
            level |= s->ch_int[first + 1] & 1;
        }
        qemu_set_irq(s->ch_irq[ch / 2], level);
    } else {
        qemu_set_irq(s->ch_irq[ch], s->ch_int[ch] & 1);
    }
}

static void edma_update_err_irq(IMXEDMAState *s)
{
    bool active = false;

    for (unsigned int i = 0; i < s->num_channels; i++) {
        if ((s->ch_es[i] & ES_ERR) &&
            (s->ch_csr[i] & CH_CSR_EEI)) {
            active = true;
        }
    }
    qemu_set_irq(s->err_irq, active);
}

static void edma_update_irqs(IMXEDMAState *s)
{
    unsigned int channel;

    for (channel = 0; channel < s->num_channels; channel++) {
        edma_update_ch_irq(s, channel);
    }
    edma_update_err_irq(s);
}

static void edma_record_error(IMXEDMAState *s, unsigned int ch, uint32_t bits)
{
    qemu_log_mask(LOG_GUEST_ERROR, "imx.edma: channel %u error 0x%x\n",
                  ch, bits);
    s->ch_es[ch] = ES_ERR | bits;
    s->mp_es = (ch << 24) | bits;
    if (s->mp_csr & MP_CSR_HAE) {
        s->mp_csr |= MP_CSR_HALT;
    }
    edma_update_err_irq(s);
}

static bool edma_req_level(IMXEDMAState *s, unsigned int ch)
{
    unsigned int line;

    if (s->version == IMX_EDMA_V3) {
        line = ch;
    } else {
        line = s->mux[ch];
        if (!line) {
            return false;       /* no request source selected */
        }
    }
    if (line >= s->num_requests) {
        return false;
    }
    return s->req[line / 32] & BIT(line % 32);
}

static bool edma_runnable(IMXEDMAState *s, unsigned int ch)
{
    uint16_t csr;

    if (s->ch_es[ch] & ES_ERR) {
        return false;           /* held until software clears the error */
    }
    csr = lduw_le_p(edma_tcd(s, ch) + s->layout->csr);
    if (csr & TCD_CSR_START) {
        return true;
    }
    return (s->ch_csr[ch] & CH_CSR_ERQ) && edma_req_level(s, ch);
}

static int edma_pick(IMXEDMAState *s)
{
    bool erca = s->mp_csr & MP_CSR_ERCA;
    int best = -1;
    uint32_t best_key = 0;

    for (unsigned int i = 0; i < s->num_channels; i++) {
        unsigned int ch = erca ? (s->rr_next + i) % s->num_channels
                           : s->num_channels - 1 - i;
        unsigned int apl = s->ch_pri[ch] & PRI_APL_MASK;
        uint32_t key;

        if (!edma_runnable(s, ch)) {
            continue;
        }
        /*
         * Group, channel priority and channel number, the highest value wins.
         */
        key = (s->grpri[ch] << 16) | (apl << 8) |
              ((erca && !apl) ? 0 : ch + 1);
        if (best < 0 || key > best_key) {
            best = ch;
            best_key = key;
        }
    }
    return best;
}

static void edma_set_start(IMXEDMAState *s, unsigned int ch)
{
    uint8_t *t;

    if (ch >= s->num_channels) {
        return;
    }
    t = edma_tcd(s, ch) + s->layout->csr;
    stw_le_p(t, lduw_le_p(t) | TCD_CSR_START);
}

static void edma_minor_loop(IMXEDMAState *s, unsigned int ch)
{
    const struct IMXEDMATcdLayout *l = s->layout;
    uint8_t *t = edma_tcd(s, ch);
    uint16_t csr = lduw_le_p(t + l->csr);
    uint16_t citer = lduw_le_p(t + l->citer);
    uint16_t biter = lduw_le_p(t + l->biter);
    uint16_t attr = lduw_le_p(t + l->attr);
    uint32_t nb_reg = ldl_le_p(t + l->nbytes);
    uint64_t saddr = tcd_get_addr(l, t, 0);
    uint64_t daddr = tcd_get_addr(l, t, l->daddr);
    int64_t soff = (int16_t)lduw_le_p(t + l->soff);
    int64_t doff = (int16_t)lduw_le_p(t + l->doff);
    unsigned int ssize = xfer_size[(attr >> 8) & 7];
    unsigned int dsize = xfer_size[attr & 7];
    unsigned int smod = (attr >> 11) & 0x1f;
    unsigned int dmod = (attr >> 3) & 0x1f;
    bool smloe = nb_reg & TCD_NBYTES_SMLOE;
    bool dmloe = nb_reg & TCD_NBYTES_DMLOE;
    bool elink = citer & 0x8000;
    unsigned int iter_mask = elink ? 0x1ff : 0x7fff;
    unsigned int link_mask = l->a64 ? 0x3f : 0x1f;
    unsigned int tmc = l->a64 ? csr >> TCD_CSR_TMC_SHIFT : 0;
    uint32_t nbytes;
    int64_t mloff = 0;
    uint32_t err = 0;
    uint8_t buf[64];
    bool have_wo = false;
    unsigned int unit;
    unsigned int cnt;

    if (smloe || dmloe) {
        nbytes = nb_reg & 0x3ff;
        mloff = sextract32(nb_reg, 10, 20);
    } else {
        nbytes = nb_reg & 0x3fffffff;
    }

    /* The channel starts: DONE and START are cleared */
    s->ch_csr[ch] &= ~CH_CSR_DONE;
    csr &= ~TCD_CSR_START;
    stw_le_p(t + l->csr, csr);

    /* Configuration errors, reported when the channel activates */
    if (!ssize || (saddr & (ssize - 1))) {
        err |= ES_SAE;
    }
    if (ssize && ((uint16_t)soff & (ssize - 1))) {
        err |= ES_SOE;
    }
    if (!dsize || (daddr & (dsize - 1))) {
        err |= ES_DAE;
    }
    if (dsize && ((uint16_t)doff & (dsize - 1))) {
        err |= ES_DOE;
    }
    if (!nbytes || (ssize && nbytes % ssize) || (dsize && nbytes % dsize) ||
        !(citer & iter_mask) || (elink != !!(biter & 0x8000))) {
        err |= ES_NCE;
    }
    if (err) {
        edma_record_error(s, ch, err);
        return;
    }

    unit = MAX(ssize, dsize);
    for (uint32_t done = 0; done < nbytes; done += unit) {
        /* Read one unit from the source, a transfer size at a time */
        for (unsigned int i = 0; i < unit; i += ssize) {
            if (tmc == 2 && have_wo) {
                break;          /* write-only mode: the source is read once */
            }
            if (address_space_read(&s->dma_as, saddr, MEMTXATTRS_UNSPECIFIED,
                                   buf + i, ssize) != MEMTX_OK) {
                err = ES_SBE;
                goto bus_error;
            }
            saddr = edma_next_addr(l, saddr, soff, smod);
        }
        have_wo = true;
        /* Write it to the destination, a transfer size at a time */
        for (unsigned int i = 0; i < unit; i += dsize) {
            if (tmc != 1 &&     /* read only mode: the data is discarded */
                address_space_write(&s->dma_as, daddr, MEMTXATTRS_UNSPECIFIED,
                                    buf + i, dsize) != MEMTX_OK) {
                err = ES_DBE;
                goto bus_error;
            }
            daddr = edma_next_addr(l, daddr, doff, dmod);
        }
    }

    cnt = (citer & iter_mask) - 1;
    if (cnt) {
        /* One iteration of the major loop is done */
        if (smloe) {
            saddr = edma_addr(l, saddr + mloff);
        }
        if (dmloe) {
            daddr = edma_addr(l, daddr + mloff);
        }
        tcd_set_addr(l, t, 0, saddr);
        tcd_set_addr(l, t, l->daddr, daddr);
        stw_le_p(t + l->citer, (citer & ~iter_mask) | cnt);
        if ((csr & TCD_CSR_INTHALF) && cnt == (biter & iter_mask) / 2) {
            s->ch_int[ch] |= 1;
        }
        if (elink && (s->mp_csr & MP_CSR_GCLC)) {
            edma_set_start(s, (citer >> 9) & link_mask);
        }
    } else {
        /* The major loop is done */
        s->ch_csr[ch] |= CH_CSR_DONE;
        if (csr & TCD_CSR_INTMAJOR) {
            s->ch_int[ch] |= 1;
        }
        if (csr & TCD_CSR_ESDA) {
            /* Store the final destination address where SLAST_SDA points */
            uint64_t ptr = edma_addr(l, tcd_get_addr(l, t, l->slast)) &
                           (l->a64 ? ~7ULL : ~3ULL);
            uint8_t v[8];

            if (l->a64) {
                stq_le_p(v, daddr);
            } else {
                stl_le_p(v, daddr);
            }
            if (address_space_write(&s->dma_as, ptr, MEMTXATTRS_UNSPECIFIED,
                                    v, l->a64 ? 8 : 4) != MEMTX_OK) {
                err = ES_DBE;
                goto bus_error;
            }
        } else {
            saddr = edma_addr(l, saddr + tcd_get_adj(l, t, l->slast));
        }
        tcd_set_addr(l, t, 0, saddr);

        if (csr & TCD_CSR_ESG) {
            /* Load the next TCD, which must be aligned to its size */
            uint64_t sga = edma_addr(l, tcd_get_addr(l, t, l->dlast));
            uint8_t next[IMX_EDMA_TCD_BYTES];

            if (sga & (l->size - 1)) {
                edma_record_error(s, ch, ES_SGE);
                goto out;
            }
            if (address_space_read(&s->dma_as, sga, MEMTXATTRS_UNSPECIFIED,
                                   next, l->size) != MEMTX_OK) {
                edma_record_error(s, ch, ES_SBE);
                goto out;
            }
            tcd_set_addr(l, t, l->daddr, daddr);
            memcpy(t, next, l->size);
        } else {
            daddr = edma_addr(l, daddr + tcd_get_adj(l, t, l->dlast));
            tcd_set_addr(l, t, l->daddr, daddr);
            stw_le_p(t + l->citer, biter);
        }
        if ((csr & TCD_CSR_MAJORELINK) && (s->mp_csr & MP_CSR_GCLC)) {
            edma_set_start(s, (csr >> 8) & link_mask);
        }
        if (csr & TCD_CSR_DREQ) {
            s->ch_csr[ch] &= ~CH_CSR_ERQ;
        }
    }
    goto out;

bus_error:
    /* The TCD keeps the address of the access that failed */
    tcd_set_addr(l, t, 0, saddr);
    tcd_set_addr(l, t, l->daddr, daddr);
    edma_record_error(s, ch, err);
out:
    edma_update_ch_irq(s, ch);
}

static void edma_bh(void *opaque)
{
    IMXEDMAState *s = opaque;
    unsigned int budget = 512;

    while (!(s->mp_csr & MP_CSR_HALT) && budget) {
        int ch = edma_pick(s);

        if (ch < 0) {
            return;
        }
        edma_minor_loop(s, ch);
        if (!(s->ch_pri[ch] & PRI_APL_MASK)) {
            s->rr_next = (ch + 1) % s->num_channels;
        }
        budget--;
    }
    if (!(s->mp_csr & MP_CSR_HALT) && edma_pick(s) >= 0) {
        qemu_bh_schedule(s->bh);
    }
}

static void edma_kick(IMXEDMAState *s)
{
    qemu_bh_schedule(s->bh);
}

static void edma_set_request(void *opaque, int n, int level)
{
    IMXEDMAState *s = opaque;

    if (level) {
        s->req[n / 32] |= BIT(n % 32);
        edma_kick(s);
    } else {
        s->req[n / 32] &= ~BIT(n % 32);
    }
}

static uint32_t edma_channel_bits(IMXEDMAState *s, unsigned int first, bool irq)
{
    uint32_t v = 0;

    for (unsigned int i = 0; i < 32 && first + i < s->num_channels; i++) {
        bool b = irq ? (s->ch_int[first + i] & 1)
                     : edma_req_level(s, first + i);

        v |= (uint32_t)b << i;
    }
    return v;
}

static uint32_t mp_read(IMXEDMAState *s, hwaddr off)
{
    if (s->version == IMX_EDMA_V5) {
        off &= 0xfff;
    }

    switch (off) {
    case MP_CSR:
        return (s->mp_csr & s->mp_csr_rw_mask) |
               ((s->version == IMX_EDMA_V3 ? 0x31 : 0x50) << MP_CSR_VER_SHIFT);
    case MP_ES: {
        uint32_t v = s->mp_es;

        for (unsigned int i = 0; i < s->num_channels; i++) {
            if (s->ch_es[i] & ES_ERR) {
                v |= MP_ES_VLD;
            }
        }
        return v;
    }
    case MP_INT_LOW:
        return edma_channel_bits(s, 0, true);
    case MP_INT_HIGH:
        return s->version == IMX_EDMA_V3 ? edma_channel_bits(s, 0, false)
                               : edma_channel_bits(s, 32, true);
    case MP_HRS_LOW:
        return s->version == IMX_EDMA_V5 ? edma_channel_bits(s, 0, false) : 0;
    case MP_HRS_HIGH:
        return s->version == IMX_EDMA_V5 ? edma_channel_bits(s, 32, false) : 0;
    default:
        break;
    }
    if (off >= MP_GRPRI && off < MP_GRPRI + 4 * s->num_channels) {
        return s->grpri[(off - MP_GRPRI) / 4];
    }
    if (s->version == IMX_EDMA_V5 && off >= MP_MUX && off < MP_MUX + 4 * s->num_channels) {
        return s->mux[(off - MP_MUX) / 4];
    }
    if (s->version == IMX_EDMA_V5 && off >= MP_PROT && off < MP_PROT + 4 * s->num_channels) {
        return s->prot[(off - MP_PROT) / 4];
    }
    return 0;
}

static void mp_write(IMXEDMAState *s, hwaddr off, uint32_t val, uint32_t wmask)
{
    if (s->version == IMX_EDMA_V5) {
        off &= 0xfff;
    }

    switch (off) {
    case MP_CSR:
        s->mp_csr = (s->mp_csr & ~(s->mp_csr_rw_mask & wmask)) |
                    (val & s->mp_csr_rw_mask & wmask);
        edma_kick(s);
        return;
    case MP_STOPCH:
        if (s->version == IMX_EDMA_V5 && (val & wmask & 0xff)) {
            unsigned int ch = val & 0x3f;

            if (ch < s->num_channels) {
                if (val & BIT(7)) {
                    edma_record_error(s, ch, ES_ECX);
                }
                stw_le_p(edma_tcd(s, ch) + s->layout->csr,
                         lduw_le_p(edma_tcd(s, ch) + s->layout->csr) &
                         ~TCD_CSR_START);
            }
        }
        return;
    default:
        break;
    }
    if (off >= MP_GRPRI && off < MP_GRPRI + 4 * s->num_channels) {
        unsigned int ch = (off - MP_GRPRI) / 4;

        s->grpri[ch] = (s->grpri[ch] & ~(0x1f & wmask)) | (val & 0x1f & wmask);
        edma_kick(s);
    } else if (s->version == IMX_EDMA_V5 && off >= MP_MUX &&
               off < MP_MUX + 4 * s->num_channels) {
        unsigned int ch = (off - MP_MUX) / 4;
        uint32_t src = val & 0x7f;

        for (unsigned int i = 0; src && i < s->num_channels; i++) {
            if (i != ch && s->mux[i] == src) {
                src = 0;
            }
        }
        s->mux[ch] = src;
        edma_kick(s);
    } else if (s->version == IMX_EDMA_V5 && off >= MP_PROT &&
               off < MP_PROT + 4 * s->num_channels) {
        unsigned int ch = (off - MP_PROT) / 4;

        s->prot[ch] = (s->prot[ch] & ~wmask) | (val & wmask);
    }
}

static uint32_t ch_read(IMXEDMAState *s, unsigned int ch, hwaddr off)
{
    switch (off) {
    case CH_CSR:
        return s->ch_csr[ch];
    case CH_ES:
        return s->ch_es[ch];
    case CH_INT:
        return s->ch_int[ch];
    case CH_SBR:
        return s->ch_sbr[ch];
    case CH_PRI:
        return s->ch_pri[ch];
    case CH_MATTR:
        return s->version == IMX_EDMA_V5 ? s->ch_mattr[ch] : 0;
    default:
        return 0;
    }
}

static void ch_write(IMXEDMAState *s, unsigned int ch, hwaddr off, uint32_t val,
                     uint32_t wmask)
{
    uint32_t rw;

    switch (off) {
    case CH_CSR:
        rw = s->csr_rw_mask & wmask;
        s->ch_csr[ch] = (s->ch_csr[ch] & ~rw) | (val & rw);
        if (val & wmask & CH_CSR_DONE) {
            s->ch_csr[ch] &= ~CH_CSR_DONE;      /* write one to clear */
        }
        if (s->version == IMX_EDMA_V5) {
            if (val & wmask & CH_CSR_CX) {
                stw_le_p(edma_tcd(s, ch) + s->layout->csr,
                         lduw_le_p(edma_tcd(s, ch) + s->layout->csr) &
                         ~TCD_CSR_START);
            }
            if (val & wmask & CH_CSR_ECX) {
                edma_record_error(s, ch, ES_ECX);
            }
        }
        edma_update_err_irq(s);
        edma_kick(s);
        break;
    case CH_ES:
        if (val & wmask & ES_ERR) {
            s->ch_es[ch] &= ~ES_ERR;
            edma_update_err_irq(s);
            edma_kick(s);
        }
        break;
    case CH_INT:
        if (val & wmask & 1) {
            s->ch_int[ch] &= ~1;
            edma_update_ch_irq(s, ch);
        }
        break;
    case CH_SBR:
        s->ch_sbr[ch] = (s->ch_sbr[ch] & ~wmask) | (val & wmask);
        break;
    case CH_PRI:
        s->ch_pri[ch] = (s->ch_pri[ch] & ~wmask) | (val & wmask);
        edma_kick(s);
        break;
    case CH_MATTR:
        if (s->version == IMX_EDMA_V5) {
            s->ch_mattr[ch] = (s->ch_mattr[ch] & ~wmask) | (val & wmask);
        }
        break;
    default:
        break;
    }
}

static bool edma_decode(IMXEDMAState *s, hwaddr off, unsigned *ch,
                        hwaddr *coff)
{
    if (off < s->ch_base) {
        return false;           /* management page */
    }
    *ch = (off - s->ch_base) / s->ch_stride;
    *coff = (off - s->ch_base) % s->ch_stride;
    return true;
}

static uint64_t edma_read(void *opaque, hwaddr off, unsigned int size)
{
    IMXEDMAState *s = opaque;
    unsigned int ch;
    hwaddr coff;
    uint32_t word;

    if (!edma_decode(s, off, &ch, &coff)) {
        word = mp_read(s, off & ~3ULL);
    } else if (ch >= s->num_channels) {
        return 0;
    } else if (coff >= CH_TCD && coff < CH_TCD + s->layout->size) {
        const uint8_t *t = edma_tcd(s, ch) + (coff - CH_TCD);
        uint64_t v = 0;

        for (unsigned int i = 0; i < size && coff - CH_TCD + i < s->layout->size;
             i++) {
            v |= (uint64_t)t[i] << (8 * i);
        }
        return v;
    } else {
        word = ch_read(s, ch, coff & ~3ULL);
    }
    return (word >> (8 * (off & 3))) & MAKE_64BIT_MASK(0, size * 8);
}

static void edma_write(void *opaque, hwaddr off, uint64_t value, unsigned int size)
{
    IMXEDMAState *s = opaque;
    unsigned int ch;
    hwaddr coff;
    unsigned int shift = 8 * (off & 3);
    uint32_t wmask = MAKE_64BIT_MASK(0, size * 8) << shift;
    uint32_t val = (uint32_t)value << shift;

    if (!edma_decode(s, off, &ch, &coff)) {
        mp_write(s, off & ~3ULL, val, wmask);
    } else if (ch >= s->num_channels) {
        return;
    } else if (coff >= CH_TCD && coff < CH_TCD + s->layout->size) {
        uint8_t *t = edma_tcd(s, ch);
        unsigned int o = coff - CH_TCD;
        unsigned int csr = s->layout->csr;

        for (unsigned int i = 0; i < size && o + i < s->layout->size; i++) {
            t[o + i] = value >> (8 * i);
        }
        if (o < csr + 2 && o + size > csr) {
            uint16_t v = lduw_le_p(t + csr);

            if (s->ch_csr[ch] & CH_CSR_DONE) {
                v &= ~(TCD_CSR_ESG | TCD_CSR_MAJORELINK);
                stw_le_p(t + csr, v);
            }
            if (v & TCD_CSR_START) {
                edma_kick(s);
            }
        }
    } else {
        ch_write(s, ch, coff & ~3ULL, val, wmask);
    }
}

static const MemoryRegionOps edma_ops = {
    .read = edma_read,
    .write = edma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void edma_reset(DeviceState *dev)
{
    IMXEDMAState *s = IMX_EDMA(dev);

    qemu_bh_cancel(s->bh);

    s->mp_csr = 0;
    s->mp_es = 0;
    s->rr_next = 0;
    memset(s->grpri, 0, sizeof(s->grpri));
    memset(s->mux, 0, sizeof(s->mux));
    memset(s->ch_csr, 0, sizeof(s->ch_csr));
    memset(s->ch_es, 0, sizeof(s->ch_es));
    memset(s->ch_int, 0, sizeof(s->ch_int));
    memset(s->ch_pri, 0, sizeof(s->ch_pri));
    memset(s->ch_mattr, 0, sizeof(s->ch_mattr));
    memset(s->tcd, 0, sizeof(s->tcd));

    for (unsigned int i = 0; i < IMX_EDMA_MAX_CHANNELS; i++) {
        s->ch_sbr[i] = 0x8007;
        s->prot[i] = 0x8007;
    }
    edma_update_irqs(s);
}

static void edma_realize(DeviceState *dev, Error **errp)
{
    IMXEDMAState *s = IMX_EDMA(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    if (s->version != IMX_EDMA_V3 && s->version != IMX_EDMA_V5) {
        error_setg(errp, "imx.edma: version must be 3 or 5");
        return;
    }
    if (!s->num_channels ||
        s->num_channels > IMX_EDMA_MAX_CHANNELS ||
        (s->version == IMX_EDMA_V3 && s->num_channels > 32)) {
        error_setg(errp, "imx.edma: invalid number of channels");
        return;
    }
    if (!s->num_requests ||
        s->num_requests > IMX_EDMA_MAX_REQUESTS) {
        error_setg(errp, "imx.edma: invalid number of requests");
        return;
    }
    if (!s->dma_mr) {
        error_setg(errp, "imx.edma: the 'dma' link is not set");
        return;
    }

    if (s->version == IMX_EDMA_V3) {
        s->layout = &tcd_v3;
        s->ch_base = 0x10000;
        s->ch_stride = 0x10000;
        s->csr_rw_mask = 0xf;
        s->mp_csr_rw_mask = MP_CSR_GMRC | MP_CSR_GCLC | MP_CSR_HALT |
                            MP_CSR_HAE | MP_CSR_ERCA | MP_CSR_EDBG;
    } else {
        s->layout = &tcd_v5;
        s->ch_base = 0x10000;
        s->ch_stride = 0x8000;
        s->csr_rw_mask = 0x003ff007;
        s->mp_csr_rw_mask = MP_CSR_GMRC | MP_CSR_GCLC | MP_CSR_HALT |
                            MP_CSR_HAE | MP_CSR_GCPC | MP_CSR_ERCA |
                            MP_CSR_EDBG;
    }

    address_space_init(&s->dma_as, s->dma_mr, "imx-edma-dma");
    s->bh = qemu_bh_new_guarded(edma_bh, s, &dev->mem_reentrancy_guard);

    memory_region_init_io(&s->iomem, OBJECT(dev), &edma_ops, s,
                          TYPE_IMX_EDMA,
                          s->ch_base +
                          (uint64_t)s->num_channels * s->ch_stride);
    sysbus_init_mmio(sbd, &s->iomem);
    for (unsigned int i = 0; i < (s->paired_irqs ? (s->num_channels + 1) / 2
                                              : s->num_channels); i++) {
        sysbus_init_irq(sbd, &s->ch_irq[i]);
    }
    sysbus_init_irq(sbd, &s->err_irq);
    qdev_init_gpio_in_named(dev, edma_set_request, "dma-req",
                            s->num_requests);
}

static void edma_unrealize(DeviceState *dev)
{
    IMXEDMAState *s = IMX_EDMA(dev);

    qemu_bh_delete(s->bh);
    s->bh = NULL;
    address_space_destroy(&s->dma_as);
}

static int edma_post_load(void *opaque, int version_id)
{
    IMXEDMAState *s = opaque;

    qemu_bh_cancel(s->bh);
    edma_update_irqs(s);
    if (!(s->mp_csr & MP_CSR_HALT) && edma_pick(s) >= 0) {
        edma_kick(s);
    }

    return 0;
}

static const VMStateDescription vmstate_imx_edma = {
    .name = TYPE_IMX_EDMA,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = edma_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(mp_csr, IMXEDMAState),
        VMSTATE_UINT32(mp_es, IMXEDMAState),
        VMSTATE_UINT32(rr_next, IMXEDMAState),
        VMSTATE_UINT32_ARRAY(grpri, IMXEDMAState, IMX_EDMA_MAX_CHANNELS),
        VMSTATE_UINT32_ARRAY(mux, IMXEDMAState, IMX_EDMA_MAX_CHANNELS),
        VMSTATE_UINT32_ARRAY(prot, IMXEDMAState, IMX_EDMA_MAX_CHANNELS),
        VMSTATE_UINT32_ARRAY(req, IMXEDMAState, IMX_EDMA_REQUEST_WORDS),
        VMSTATE_UINT32_ARRAY(ch_csr, IMXEDMAState, IMX_EDMA_MAX_CHANNELS),
        VMSTATE_UINT32_ARRAY(ch_es, IMXEDMAState, IMX_EDMA_MAX_CHANNELS),
        VMSTATE_UINT32_ARRAY(ch_int, IMXEDMAState, IMX_EDMA_MAX_CHANNELS),
        VMSTATE_UINT32_ARRAY(ch_sbr, IMXEDMAState, IMX_EDMA_MAX_CHANNELS),
        VMSTATE_UINT32_ARRAY(ch_pri, IMXEDMAState, IMX_EDMA_MAX_CHANNELS),
        VMSTATE_UINT32_ARRAY(ch_mattr, IMXEDMAState, IMX_EDMA_MAX_CHANNELS),
        VMSTATE_UINT8_ARRAY(tcd, IMXEDMAState,
                            IMX_EDMA_MAX_CHANNELS * IMX_EDMA_TCD_BYTES),
        VMSTATE_END_OF_LIST()
    },
};

static const Property edma_props[] = {
    DEFINE_PROP_UINT32("version", IMXEDMAState, version, IMX_EDMA_V5),
    DEFINE_PROP_UINT32("num-channels", IMXEDMAState, num_channels, 64),
    DEFINE_PROP_UINT32("num-requests", IMXEDMAState, num_requests, 128),
    DEFINE_PROP_BOOL("paired-irqs", IMXEDMAState, paired_irqs, false),
    DEFINE_PROP_LINK("dma", IMXEDMAState, dma_mr, TYPE_MEMORY_REGION,
                     MemoryRegion *),
};

static void edma_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = edma_realize;
    dc->unrealize = edma_unrealize;
    device_class_set_legacy_reset(dc, edma_reset);
    dc->vmsd = &vmstate_imx_edma;
    dc->desc = "NXP i.MX eDMA3/eDMA5 controller";
    device_class_set_props(dc, edma_props);
}

static const TypeInfo edma_info = {
    .name = TYPE_IMX_EDMA,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMXEDMAState),
    .class_init = edma_class_init,
};

static void edma_register_types(void)
{
    type_register_static(&edma_info);
}

type_init(edma_register_types)

