/*
 * NXP i.MX95 FlexSPI
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "hw/core/irq.h"
#include "hw/misc/imx95_fspi.h"
#include "migration/vmstate.h"
#include "trace.h"

#define FSPI_MCR0               0x000
#define FSPI_MCR0_SWRST         BIT(0)
#define FSPI_MCR1               0x004
#define FSPI_MCR2               0x008
#define FSPI_MCR2_SAMEDEVICEEN  BIT(15)
#define FSPI_AHBCR              0x00c
#define FSPI_INTEN              0x010
#define FSPI_INTR               0x014
#define FSPI_LUTKEY             0x018
#define FSPI_LUTKEY_VALUE       0x5af05af0
#define FSPI_LUTCR              0x01c
#define FSPI_LUTCR_LOCK         BIT(0)
#define FSPI_LUTCR_UNLOCK       BIT(1)
#define FSPI_AHBRXBUF0CR0       0x020
#define FSPI_FLSHA1CR0          0x060
#define FSPI_FLSHA1CR2          0x080
#define FSPI_FLSHCR2_CLRINSTRPTR BIT(31)
#define FSPI_FLSHSZ_MASK        0x007fffff
#define FSPI_IPCR0              0x0a0
#define FSPI_IPCR1              0x0a4
#define FSPI_IPCMD              0x0b0
#define FSPI_IPCMD_TRG          BIT(0)
#define FSPI_IPRXFCR            0x0b8
#define FSPI_IPTXFCR            0x0bc
#define FSPI_FCR_CLR            BIT(0)
#define FSPI_FCR_WMRK_SHIFT     2
#define FSPI_FCR_WMRK_MASK      0x3f
#define FSPI_DLLACR             0x0c0
#define FSPI_DLLBCR             0x0c4
#define FSPI_DLL_EN             BIT(0)
#define FSPI_DLL_RESET          BIT(1)
#define FSPI_STS0               0x0e0
#define FSPI_STS0_SEQIDLE       BIT(0)
#define FSPI_STS0_ARBIDLE       BIT(1)
#define FSPI_STS0_CMDSRC_SHIFT  2
#define FSPI_STS1               0x0e4
#define FSPI_STS2               0x0e8
#define FSPI_STS2_RESET         0x01000100
#define FSPI_AHBSPNDSTS         0x0ec
#define FSPI_IPRXFSTS           0x0f0
#define FSPI_IPTXFSTS           0x0f4
#define FSPI_RFDR               0x100
#define FSPI_TFDR               0x180
#define FSPI_LUT                0x200
#define FSPI_LUT_END            0x400

#define FSPI_INTR_IPCMDDONE     BIT(0)
#define FSPI_INTR_IPCMDGE       BIT(1)
#define FSPI_INTR_AHBCMDGE      BIT(2)
#define FSPI_INTR_IPCMDERR      BIT(3)
#define FSPI_INTR_AHBCMDERR     BIT(4)
#define FSPI_INTR_IPRXWA        BIT(5)
#define FSPI_INTR_IPTXWE        BIT(6)
#define FSPI_INTR_SCKSTOPBYRD   BIT(8)
#define FSPI_INTR_SCKSTOPBYWR   BIT(9)
#define FSPI_INTR_AHBBUSERR     BIT(10)
#define FSPI_INTR_FIFO_BITS     (FSPI_INTR_IPRXWA | FSPI_INTR_IPTXWE)

#define FSPI_ERR_JMP_ON_CS      0x2
#define FSPI_ERR_UNKNOWN_INSTR  0x3
#define FSPI_ERR_ADDR_RANGE     0x6
#define FSPI_ERR_BOUNDARY       0xf

#define FSPI_OP_STOP            0x00
#define FSPI_OP_CMD             0x01
#define FSPI_OP_RADDR           0x02
#define FSPI_OP_CADDR           0x03
#define FSPI_OP_MODE1           0x04
#define FSPI_OP_MODE2           0x05
#define FSPI_OP_MODE4           0x06
#define FSPI_OP_MODE8           0x07
#define FSPI_OP_WRITE           0x08
#define FSPI_OP_READ            0x09
#define FSPI_OP_LEARN           0x0a
#define FSPI_OP_DATSZ           0x0b
#define FSPI_OP_DUMMY           0x0c
#define FSPI_OP_JMP_ON_CS       0x1f

#define FSPI_INSTR_PER_SEQ      8
#define FSPI_WORDS_PER_SEQ      4

#define FSPI_AHB_FILL           256     /* bytes read per flash sequence */

#define FSPI_REG(off)           ((off) / sizeof(uint32_t))

static const struct {
    uint32_t offset;
    uint32_t value;
} imx95_fspi_reset_values[] = {
    { FSPI_MCR0, 0xffff8002 },
    { FSPI_MCR1, 0xffffffff },
    { FSPI_MCR2, 0x200081f7 },
    { FSPI_AHBCR, 0x00000018 },
    { FSPI_LUTKEY, FSPI_LUTKEY_VALUE },
    { FSPI_LUTCR, FSPI_LUTCR_UNLOCK },
    { 0x70, 0x63 }, { 0x74, 0x63 }, { 0x78, 0x63 }, { 0x7c, 0x63 },
    { 0x94, 0xc3 },
    { FSPI_DLLACR, 0x100 }, { FSPI_DLLBCR, 0x100 },
};

static unsigned imx95_fspi_access_shift(hwaddr offset)
{
    return (offset & 3U) * 8U;
}

static uint32_t imx95_fspi_access_mask(hwaddr offset, unsigned int size)
{
    unsigned int shift = imx95_fspi_access_shift(offset);

    switch (size) {
    case 1:
        return (uint32_t)UINT8_MAX << shift;
    case 2:
        return (uint32_t)UINT16_MAX << shift;
    default:
        return UINT32_MAX;
    }
}

static unsigned imx95_fspi_fifo_wmrk_bytes(uint32_t fcr)
{
    return (((fcr >> FSPI_FCR_WMRK_SHIFT) & FSPI_FCR_WMRK_MASK) + 1) * 8;
}

static uint32_t imx95_fspi_rx_fill_bytes(Imx95FspiState *s)
{
    return s->rx_len - s->rx_head;
}

static uint32_t imx95_fspi_intr_view(Imx95FspiState *s)
{
    uint32_t view = s->intr;

    if (imx95_fspi_rx_fill_bytes(s) >=
        imx95_fspi_fifo_wmrk_bytes(s->regs[FSPI_REG(FSPI_IPRXFCR)])) {
        view |= FSPI_INTR_IPRXWA;
    }
    if (s->tx_len + imx95_fspi_fifo_wmrk_bytes(
                        s->regs[FSPI_REG(FSPI_IPTXFCR)]) <= IMX95_FSPI_TX_MAX) {
        view |= FSPI_INTR_IPTXWE;
    }
    return view;
}

static void imx95_fspi_update_irq(Imx95FspiState *s)
{
    qemu_set_irq(s->irq, !!(imx95_fspi_intr_view(s) &
                            s->regs[FSPI_REG(FSPI_INTEN)]));
}

/* Size in bytes of the four flash devices, in AHB address order */
static void imx95_fspi_device_sizes(Imx95FspiState *s,
                                    uint64_t size[IMX95_FSPI_NUM_CS])
{
    bool same = s->regs[FSPI_REG(FSPI_MCR2)] & FSPI_MCR2_SAMEDEVICEEN;
    uint64_t first = s->regs[FSPI_REG(FSPI_FLSHA1CR0)] & FSPI_FLSHSZ_MASK;

    for (unsigned i = 0; i < IMX95_FSPI_NUM_CS; i++) {
        uint64_t kb = same ? first :
            s->regs[FSPI_REG(FSPI_FLSHA1CR0) + i] & FSPI_FLSHSZ_MASK;

        size[i] = kb * KiB;
    }
}

/* Find the device that a flash address belongs to and the address in it */
static bool imx95_fspi_find_cs(Imx95FspiState *s, uint64_t addr,
                               unsigned *cs, uint32_t *dev_addr,
                               uint64_t *dev_size)
{
    uint64_t size[IMX95_FSPI_NUM_CS];
    uint64_t base = 0;

    imx95_fspi_device_sizes(s, size);
    for (unsigned i = 0; i < IMX95_FSPI_NUM_CS; i++) {
        if (addr >= base && addr - base < size[i]) {
            *cs = i;
            *dev_addr = addr - base;
            *dev_size = size[i];
            return true;
        }
        base += size[i];
    }
    return false;
}

static uint16_t imx95_fspi_instr(Imx95FspiState *s, unsigned seq, unsigned idx)
{
    uint32_t word = s->regs[FSPI_REG(FSPI_LUT) + seq * FSPI_WORDS_PER_SEQ +
                            idx / 2];

    return word >> ((idx & 1) * 16);
}

static bool imx95_fspi_opcode_known(unsigned opcode)
{
    return opcode <= FSPI_OP_DUMMY || opcode == FSPI_OP_JMP_ON_CS;
}

/* Reject a bad sequence before anything is sent to the flash */
static unsigned imx95_fspi_check_seqs(Imx95FspiState *s, unsigned first,
                                      unsigned num, bool allow_jmp,
                                      unsigned start, unsigned *bad_seq)
{
    for (unsigned seq = first; seq < first + num; seq++) {
        if (seq >= IMX95_FSPI_NUM_SEQ) {
            *bad_seq = seq;
            return FSPI_ERR_UNKNOWN_INSTR;
        }
        for (unsigned i = seq == first ? start : 0;
             i < FSPI_INSTR_PER_SEQ; i++) {
            unsigned opcode = imx95_fspi_instr(s, seq, i) >> 10;

            if (opcode == FSPI_OP_STOP) {
                break;
            }
            *bad_seq = seq;
            if (opcode == FSPI_OP_JMP_ON_CS) {
                return allow_jmp ? 0 : FSPI_ERR_JMP_ON_CS;
            }
            if (!imx95_fspi_opcode_known(opcode)) {
                return FSPI_ERR_UNKNOWN_INSTR;
            }
        }
    }
    return 0;
}

typedef struct Imx95FspiExec {
    Imx95FspiState *s;
    unsigned cs;
    uint32_t addr;          /* address in the flash device */
    uint32_t datsz;         /* number of bytes of the data phase */
    uint8_t *rd;            /* READ stores */
    uint32_t rd_cap;
    uint32_t rd_len;
    const uint8_t *wr;      /* WRITE takes the bytes from here */
    uint32_t wr_len;
    uint32_t wr_pos;
    uint8_t acc;            /* bits of the byte that is being assembled */
    unsigned nbits;
    unsigned lines;         /* lines of the last command or address phase */
    bool present;           /* a peripheral is on the SPI bus or not */
} Imx95FspiExec;

static uint8_t imx95_fspi_xfer(Imx95FspiExec *x, uint8_t out)
{
    return x->present ? ssi_transfer(x->s->spi, out) : 0xff;
}

static void imx95_fspi_tx_bits(Imx95FspiExec *x, uint32_t value,
                               unsigned nbits)
{
    while (nbits) {
        unsigned take = MIN(nbits, 8 - x->nbits);
        uint32_t bits = (value >> (nbits - take)) & ((1U << take) - 1);

        x->acc = (x->acc << take) | bits;
        x->nbits += take;
        nbits -= take;
        if (x->nbits == 8) {
            imx95_fspi_xfer(x, x->acc);
            x->acc = 0;
            x->nbits = 0;
        }
    }
}

/* Finish a byte that has less than eight bits, as the clock does */
static void imx95_fspi_flush_bits(Imx95FspiExec *x)
{
    if (x->nbits) {
        imx95_fspi_tx_bits(x, 0, 8 - x->nbits);
    }
}

/* Run one sequence, from instruction @start. Returns the JMP_ON_CS operand. */
static int imx95_fspi_run_seq(Imx95FspiExec *x, unsigned seq, unsigned start)
{
    Imx95FspiState *s = x->s;
    int jump = -1;

    qemu_set_irq(s->cs[x->cs], 0);
    x->acc = 0;
    x->nbits = 0;
    x->lines = 1;

    for (unsigned i = start; i < FSPI_INSTR_PER_SEQ; i++) {
        uint16_t instr = imx95_fspi_instr(s, seq, i);
        unsigned opcode = instr >> 10;
        unsigned lines = 1U << ((instr >> 8) & 3);
        unsigned operand = instr & 0xff;

        trace_imx95_fspi_instr(seq, i, opcode, lines, operand);
        if (opcode == FSPI_OP_STOP) {
            break;
        }
        if (opcode == FSPI_OP_JMP_ON_CS) {
            jump = operand;
            break;
        }

        switch (opcode) {
        case FSPI_OP_CMD:
            imx95_fspi_tx_bits(x, operand, 8);
            x->lines = lines;
            break;
        case FSPI_OP_RADDR:
            imx95_fspi_tx_bits(x, x->addr, MIN(operand, 32));
            x->lines = lines;
            break;
        case FSPI_OP_CADDR:
            qemu_log_mask(LOG_UNIMP, "imx95.fspi: column address "
                          "instruction ignored\n");
            break;
        case FSPI_OP_MODE1 ... FSPI_OP_MODE8: {
            unsigned bits = 1U << (opcode - FSPI_OP_MODE1);

            imx95_fspi_tx_bits(x, operand & ((1U << bits) - 1), bits);
            x->lines = lines;
            break;
        }
        case FSPI_OP_DUMMY: {
            unsigned bits = operand * x->lines;

            while (bits) {
                unsigned n = MIN(bits, 32);

                imx95_fspi_tx_bits(x, 0, n);
                bits -= n;
            }
            break;
        }
        case FSPI_OP_DATSZ:
            x->datsz = operand;
            break;
        case FSPI_OP_LEARN:
            break;
        case FSPI_OP_READ:
            imx95_fspi_flush_bits(x);
            for (uint32_t k = 0; k < x->datsz; k++) {
                uint8_t v = imx95_fspi_xfer(x, 0);

                if (x->rd && k < x->rd_cap) {
                    x->rd[k] = v;
                }
            }
            x->rd_len = MIN(x->datsz, x->rd_cap);
            break;
        case FSPI_OP_WRITE:
            imx95_fspi_flush_bits(x);
            for (uint32_t k = 0; k < x->datsz; k++) {
                uint8_t b = 0xff;

                if (x->wr_pos < x->wr_len) {
                    b = x->wr[x->wr_pos++];
                } else {
                    s->intr |= FSPI_INTR_SCKSTOPBYWR;
                }
                imx95_fspi_xfer(x, b);
            }
            break;
        default:
            break;
        }
    }

    imx95_fspi_flush_bits(x);
    qemu_set_irq(s->cs[x->cs], 1);
    return jump;
}

static void imx95_fspi_ip_error(Imx95FspiState *s, unsigned code,
                                unsigned seq)
{
    trace_imx95_fspi_error("ip", code, seq);
    s->intr |= FSPI_INTR_IPCMDERR | FSPI_INTR_IPCMDDONE;
    s->sts1 = (s->sts1 & 0x0000ffff) | (code << 24) | (seq << 16);
}

static void imx95_fspi_ip_command(Imx95FspiState *s)
{
    uint32_t ipcr1 = s->regs[FSPI_REG(FSPI_IPCR1)];
    unsigned seq = (ipcr1 >> 16) & 0x1f;
    unsigned num = ((ipcr1 >> 24) & 7) + 1;
    uint32_t datsz = ipcr1 & 0xffff;
    uint32_t sfar = s->regs[FSPI_REG(FSPI_IPCR0)];
    Imx95FspiExec x = { .s = s, .datsz = datsz };
    uint64_t dev_size;
    unsigned bad_seq = seq;
    unsigned err;

    s->sts0 = 2U << FSPI_STS0_CMDSRC_SHIFT;
    s->ahb_valid = false;
    trace_imx95_fspi_ip(sfar, seq, num, datsz);

    if (!imx95_fspi_find_cs(s, sfar, &x.cs, &x.addr, &dev_size)) {
        imx95_fspi_ip_error(s, FSPI_ERR_ADDR_RANGE, seq);
        return;
    }
    err = imx95_fspi_check_seqs(s, seq, num, false, 0, &bad_seq);
    if (err) {
        imx95_fspi_ip_error(s, err, bad_seq);
        return;
    }
    if ((uint64_t)x.addr + datsz > dev_size) {
        imx95_fspi_ip_error(s, FSPI_ERR_BOUNDARY, seq);
        return;
    }

    x.present = !QTAILQ_EMPTY(&BUS(s->spi)->children);
    x.wr = s->tx_buf;
    x.wr_len = s->tx_len;
    /* The read data is stored in 64-bit entries, padded with zeros */
    x.rd = s->rx_buf + s->rx_len;
    x.rd_cap = IMX95_FSPI_RX_MAX - s->rx_len;
    if (datsz > x.rd_cap) {
        qemu_log_mask(LOG_GUEST_ERROR, "imx95.fspi: IP read of %u bytes does "
                      "not fit the receive FIFO\n", datsz);
        s->intr |= FSPI_INTR_SCKSTOPBYRD;
    }
    for (unsigned k = 0; k < num; k++) {
        imx95_fspi_run_seq(&x, seq + k, 0);
    }
    if (x.rd_len) {
        uint32_t padded = ROUND_UP(x.rd_len, 8);

        memset(s->rx_buf + s->rx_len + x.rd_len, 0,
               MIN(padded, x.rd_cap) - x.rd_len);
        s->rx_len += MIN(padded, x.rd_cap);
    }
    s->tx_len = 0;
    s->intr |= FSPI_INTR_IPCMDDONE;
}

static MemTxResult imx95_fspi_ahb_read(void *opaque, hwaddr addr,
                                       uint64_t *data, unsigned size,
                                       MemTxAttrs attrs)
{
    Imx95FspiState *s = opaque;
    uint64_t v = 0;

    if (!(s->ahb_valid && addr >= s->ahb_base &&
          addr + size <= (uint64_t)s->ahb_base + s->ahb_len)) {
        hwaddr line = addr & ~(hwaddr)(FSPI_AHB_FILL - 1);
        Imx95FspiExec x = { .s = s };
        uint64_t dev_size;
        uint32_t dev_addr;
        uint32_t cr2;
        unsigned seq, num, start, bad_seq = 0, err;

        if (!imx95_fspi_find_cs(s, line, &x.cs, &dev_addr, &dev_size)) {
            s->intr |= FSPI_INTR_AHBBUSERR;
            imx95_fspi_update_irq(s);
            return MEMTX_ERROR;
        }
        x.addr = dev_addr;
        x.datsz = MIN((uint64_t)FSPI_AHB_FILL, dev_size - dev_addr);

        cr2 = s->regs[FSPI_REG(FSPI_FLSHA1CR2) + x.cs];
        seq = cr2 & 0x1f;
        num = ((cr2 >> 5) & 7) + 1;
        start = s->instr_ptr[x.cs];
        trace_imx95_fspi_ahb(x.cs, x.addr, x.datsz, seq);

        err = imx95_fspi_check_seqs(s, seq, num, true, start, &bad_seq);
        if (err) {
            trace_imx95_fspi_error("ahb", err, bad_seq);
            s->intr |= FSPI_INTR_AHBCMDERR;
            s->sts1 = (s->sts1 & 0xffff0000) | (err << 8) | bad_seq;
            imx95_fspi_update_irq(s);
            return MEMTX_ERROR;
        }

        s->sts0 = 0;
        x.present = !QTAILQ_EMPTY(&BUS(s->spi)->children);
        x.rd = s->ahb_buf;
        x.rd_cap = sizeof(s->ahb_buf);
        for (unsigned k = 0; k < num; k++) {
            int jump = imx95_fspi_run_seq(&x, seq + k, k ? 0 : start);

            if (jump >= 0) {
                s->instr_ptr[x.cs] = jump;
                break;
            }
        }
        s->ahb_base = line;
        s->ahb_len = x.rd_len;
        s->ahb_valid = x.rd_len != 0;
        if (!s->ahb_valid) {
            return MEMTX_ERROR;
        }
    }

    for (unsigned i = 0; i < size; i++) {
        v |= (uint64_t)s->ahb_buf[addr - s->ahb_base + i] << (8 * i);
    }
    *data = v;
    return MEMTX_OK;
}

static MemTxResult imx95_fspi_ahb_write(void *opaque, hwaddr addr,
                                        uint64_t data, unsigned size,
                                        MemTxAttrs attrs)
{
    qemu_log_mask(LOG_UNIMP, "imx95.fspi: AHB write to flash at 0x%"
                  HWADDR_PRIx " is not implemented\n", addr);
    return MEMTX_OK;
}

static const MemoryRegionOps imx95_fspi_ahb_ops = {
    .read_with_attrs = imx95_fspi_ahb_read,
    .write_with_attrs = imx95_fspi_ahb_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = true,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static uint32_t imx95_fspi_dll_status(Imx95FspiState *s)
{
    uint32_t a = s->regs[FSPI_REG(FSPI_DLLACR)];
    uint32_t b = s->regs[FSPI_REG(FSPI_DLLBCR)];
    uint32_t status = FSPI_STS2_RESET;

    /* The reference and the target delay line lock once the DLL runs */
    if ((a & FSPI_DLL_EN) && !(a & FSPI_DLL_RESET)) {
        status |= BIT(1) | BIT(0);
    }
    if ((b & FSPI_DLL_EN) && !(b & FSPI_DLL_RESET)) {
        status |= BIT(17) | BIT(16);
    }
    return status;
}

static uint32_t imx95_fspi_read_word(Imx95FspiState *s, hwaddr off)
{
    switch (off) {
    case FSPI_MCR0:
        return s->regs[FSPI_REG(off)] & ~FSPI_MCR0_SWRST;
    case FSPI_INTR:
        return imx95_fspi_intr_view(s);
    case FSPI_LUTCR:
        return s->regs[FSPI_REG(off)];
    case FSPI_STS0:
        return FSPI_STS0_ARBIDLE | FSPI_STS0_SEQIDLE | s->sts0;
    case FSPI_STS1:
        return s->sts1;
    case FSPI_STS2:
        return imx95_fspi_dll_status(s);
    case FSPI_AHBSPNDSTS:
        return 0;
    case FSPI_IPRXFSTS:
        return MIN(imx95_fspi_rx_fill_bytes(s) / 8, 255);
    case FSPI_IPTXFSTS:
        return MIN(s->tx_len / 8, 255);
    case FSPI_IPCMD:
        return 0;
    default:
        break;
    }

    if (off >= FSPI_RFDR && off < FSPI_TFDR) {
        /* The window shows the head of the FIFO */
        uint32_t pos = s->rx_head + (off - FSPI_RFDR);
        uint8_t w[4] = { 0 };

        for (unsigned i = 0; i < 4; i++) {
            if (pos + i < s->rx_len) {
                w[i] = s->rx_buf[pos + i];
            }
        }
        return ldl_le_p(w);
    }
    if (off >= FSPI_TFDR && off < FSPI_LUT) {
        return 0;
    }
    return s->regs[FSPI_REG(off)];
}

static uint64_t imx95_fspi_read(void *opaque, hwaddr offset,
                                unsigned int size)
{
    Imx95FspiState *s = opaque;
    hwaddr word = offset & ~(hwaddr)3;
    uint32_t value;

    if (word >= IMX95_FSPI_REG_SIZE) {
        return 0;
    }
    value = imx95_fspi_read_word(s, word);
    return (value & imx95_fspi_access_mask(offset, size)) >>
           imx95_fspi_access_shift(offset);
}

static void imx95_fspi_soft_reset(Imx95FspiState *s)
{
    s->ahb_valid = false;
    s->rx_len = 0;
    s->rx_head = 0;
    s->tx_len = 0;
}

static void imx95_fspi_intr_write(Imx95FspiState *s, uint32_t value)
{
    uint32_t clear = value & s->intr;

    if (value & FSPI_INTR_IPRXWA) {
        /* Clearing the flag pops a watermark level of data */
        s->rx_head = MIN(s->rx_head + imx95_fspi_fifo_wmrk_bytes(
                             s->regs[FSPI_REG(FSPI_IPRXFCR)]), s->rx_len);
        if (s->rx_head == s->rx_len) {
            s->rx_head = 0;
            s->rx_len = 0;
        }
    }
    if (value & FSPI_INTR_IPTXWE) {
        /* Clearing the flag pushes a watermark level into the FIFO */
        uint32_t n = MIN(imx95_fspi_fifo_wmrk_bytes(
                             s->regs[FSPI_REG(FSPI_IPTXFCR)]),
                         IMX95_FSPI_TX_WINDOW);

        n = MIN(n, IMX95_FSPI_TX_MAX - s->tx_len);
        memcpy(s->tx_buf + s->tx_len, s->tx_win, n);
        s->tx_len += n;
    }
    s->intr &= ~clear;
    if (clear & FSPI_INTR_IPCMDERR) {
        s->sts1 &= 0x0000ffff;
    }
    if (clear & FSPI_INTR_AHBCMDERR) {
        s->sts1 &= 0xffff0000;
    }
}

static void imx95_fspi_write_word(Imx95FspiState *s, hwaddr off,
                                  uint32_t value, uint32_t mask)
{
    uint32_t *reg = &s->regs[FSPI_REG(off)];

    if (off != FSPI_LUTCR && off != FSPI_LUTKEY) {
        s->lut_key_armed = false;
    }

    switch (off) {
    case FSPI_MCR0:
        *reg = (*reg & ~mask) | (value & mask & ~FSPI_MCR0_SWRST);
        if (value & mask & FSPI_MCR0_SWRST) {
            imx95_fspi_soft_reset(s);
        }
        break;
    case FSPI_INTR:
        imx95_fspi_intr_write(s, value & mask);
        break;
    case FSPI_LUTKEY:
        *reg = (*reg & ~mask) | (value & mask);
        s->lut_key_armed = *reg == FSPI_LUTKEY_VALUE;
        break;
    case FSPI_LUTCR:
        if (s->lut_key_armed) {
            switch (value & (FSPI_LUTCR_LOCK | FSPI_LUTCR_UNLOCK)) {
            case FSPI_LUTCR_LOCK:
            case FSPI_LUTCR_UNLOCK:
                *reg = (*reg & ~(FSPI_LUTCR_LOCK | FSPI_LUTCR_UNLOCK)) |
                       (value & (FSPI_LUTCR_LOCK | FSPI_LUTCR_UNLOCK));
                trace_imx95_fspi_lut_lock(!!(*reg & FSPI_LUTCR_LOCK));
                break;
            default:
                qemu_log_mask(LOG_GUEST_ERROR, "imx95.fspi: LUTCR write "
                              "with LOCK and UNLOCK both %u\n",
                              value & (FSPI_LUTCR_LOCK | FSPI_LUTCR_UNLOCK) ?
                              1 : 0);
                break;
            }
        }
        s->lut_key_armed = false;
        break;
    case FSPI_IPCMD:
        if (value & mask & FSPI_IPCMD_TRG) {
            imx95_fspi_ip_command(s);
        }
        break;
    case FSPI_IPRXFCR:
        *reg = (*reg & ~mask) | (value & mask & ~FSPI_FCR_CLR);
        if (value & mask & FSPI_FCR_CLR) {
            s->rx_len = 0;
            s->rx_head = 0;
        }
        break;
    case FSPI_IPTXFCR:
        *reg = (*reg & ~mask) | (value & mask & ~FSPI_FCR_CLR);
        if (value & mask & FSPI_FCR_CLR) {
            s->tx_len = 0;
        }
        break;
    case FSPI_STS0:
    case FSPI_STS1:
    case FSPI_STS2:
    case FSPI_AHBSPNDSTS:
    case FSPI_IPRXFSTS:
    case FSPI_IPTXFSTS:
        qemu_log_mask(LOG_GUEST_ERROR, "imx95.fspi: write to the read-only "
                      "register at 0x%" HWADDR_PRIx "\n", off);
        break;
    default:
        if (off >= FSPI_RFDR && off < FSPI_TFDR) {
            qemu_log_mask(LOG_GUEST_ERROR, "imx95.fspi: write to the "
                          "receive FIFO window\n");
        } else if (off >= FSPI_TFDR && off < FSPI_LUT) {
            uint32_t w = ldl_le_p(s->tx_win + (off - FSPI_TFDR));

            w = (w & ~mask) | (value & mask);
            stl_le_p(s->tx_win + (off - FSPI_TFDR), w);
        } else if (off >= FSPI_LUT && off < FSPI_LUT_END) {
            if (s->regs[FSPI_REG(FSPI_LUTCR)] & FSPI_LUTCR_LOCK) {
                qemu_log_mask(LOG_GUEST_ERROR, "imx95.fspi: write to the "
                              "locked LUT\n");
            } else {
                *reg = (*reg & ~mask) | (value & mask);
            }
        } else if (off >= FSPI_FLSHA1CR2 && off < FSPI_FLSHA1CR2 + 16) {
            /* CLRINSTRPTR clears the pointer that JMP_ON_CS saved */
            if (value & mask & FSPI_FLSHCR2_CLRINSTRPTR) {
                s->instr_ptr[(off - FSPI_FLSHA1CR2) / 4] = 0;
            }
            *reg = (*reg & ~mask) |
                   (value & mask & ~FSPI_FLSHCR2_CLRINSTRPTR);
        } else {
            *reg = (*reg & ~mask) | (value & mask);
        }
        break;
    }
}

static void imx95_fspi_write(void *opaque, hwaddr offset, uint64_t value,
                             unsigned int size)
{
    Imx95FspiState *s = opaque;
    hwaddr word = offset & ~(hwaddr)3;
    uint32_t mask = imx95_fspi_access_mask(offset, size);

    if (word >= IMX95_FSPI_REG_SIZE) {
        return;
    }
    imx95_fspi_write_word(s, word,
                          ((uint32_t)value << imx95_fspi_access_shift(offset)),
                          mask);
    imx95_fspi_update_irq(s);
}

static const MemoryRegionOps imx95_fspi_ops = {
    .read = imx95_fspi_read,
    .write = imx95_fspi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void imx95_fspi_reset(DeviceState *dev)
{
    Imx95FspiState *s = IMX95_FSPI(dev);

    memset(s->regs, 0, sizeof(s->regs));
    for (unsigned i = 0; i < ARRAY_SIZE(imx95_fspi_reset_values); i++) {
        s->regs[FSPI_REG(imx95_fspi_reset_values[i].offset)] =
            imx95_fspi_reset_values[i].value;
    }
    for (unsigned i = 0; i < 8; i++) {
        s->regs[FSPI_REG(FSPI_AHBRXBUF0CR0) + i] = 0x80000020 | (i << 16);
    }
    s->intr = 0;
    s->sts0 = 0;
    s->sts1 = 0;
    s->lut_key_armed = false;
    s->ahb_valid = false;
    s->rx_len = 0;
    s->rx_head = 0;
    s->tx_len = 0;
    memset(s->tx_win, 0, sizeof(s->tx_win));
    memset(s->instr_ptr, 0, sizeof(s->instr_ptr));

    /* The chip selects are active low */
    for (unsigned i = 0; i < IMX95_FSPI_NUM_CS; i++) {
        qemu_set_irq(s->cs[i], 1);
    }
    imx95_fspi_update_irq(s);
}

static void imx95_fspi_init(Object *obj)
{
    Imx95FspiState *s = IMX95_FSPI(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_fspi_ops, s,
                          TYPE_IMX95_FSPI, IMX95_FSPI_REG_SIZE);
    memory_region_init_io(&s->ahb_iomem, obj, &imx95_fspi_ahb_ops, s,
                          "imx95.fspi-ahb", IMX95_FSPI_AHB_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_mmio(sbd, &s->ahb_iomem);
    sysbus_init_irq(sbd, &s->irq);

    s->spi = ssi_create_bus(DEVICE(obj), "spi");
    qdev_init_gpio_out_named(DEVICE(obj), s->cs, "cs", IMX95_FSPI_NUM_CS);
}

static const VMStateDescription vmstate_imx95_fspi = {
    .name = TYPE_IMX95_FSPI,
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, Imx95FspiState, IMX95_FSPI_REG_COUNT),
        VMSTATE_UINT32(intr, Imx95FspiState),
        VMSTATE_UINT32(sts0, Imx95FspiState),
        VMSTATE_UINT32(sts1, Imx95FspiState),
        VMSTATE_BOOL(lut_key_armed, Imx95FspiState),
        VMSTATE_UINT32(rx_len, Imx95FspiState),
        VMSTATE_UINT32(rx_head, Imx95FspiState),
        VMSTATE_BUFFER(rx_buf, Imx95FspiState),
        VMSTATE_UINT32(tx_len, Imx95FspiState),
        VMSTATE_BUFFER(tx_buf, Imx95FspiState),
        VMSTATE_BUFFER(tx_win, Imx95FspiState),
        VMSTATE_UINT8_ARRAY(instr_ptr, Imx95FspiState, IMX95_FSPI_NUM_CS),
        VMSTATE_END_OF_LIST()
    },
};

static void imx95_fspi_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, imx95_fspi_reset);
    dc->vmsd = &vmstate_imx95_fspi;
    dc->desc = "i.MX95 FlexSPI";
}

static const TypeInfo imx95_fspi_types[] = {
    {
        .name = TYPE_IMX95_FSPI,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Imx95FspiState),
        .instance_init = imx95_fspi_init,
        .class_init = imx95_fspi_class_init,
    },
};

DEFINE_TYPES(imx95_fspi_types)
