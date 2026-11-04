/*
 * i.MX LPUART(Low Power UART)
 *
 * Low-power UART: register model with a character device backend, a
 * receive FIFO and an interrupt line. Transmission completes immediately.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 * Author: Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/char/imx_lpuart.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"

#ifndef DEBUG_IMX_LPUART
#define DEBUG_IMX_LPUART 0
#endif

#define DPRINTF(fmt, args...) \
    do { \
        if (DEBUG_IMX_LPUART) { \
            fprintf(stderr, "[%s]%s: " fmt , TYPE_IMX_LPUART, \
                                             __func__, ##args); \
        } \
    } while (0)

static const VMStateDescription vmstate_imx_lpuart = {
    .name = TYPE_IMX_LPUART,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(verid, IMXLPUARTState),
        VMSTATE_UINT32(param, IMXLPUARTState),
        VMSTATE_UINT32(global, IMXLPUARTState),
        VMSTATE_UINT32(pincfg, IMXLPUARTState),
        VMSTATE_UINT32(baud, IMXLPUARTState),
        VMSTATE_UINT32(stat, IMXLPUARTState),
        VMSTATE_UINT32(ctrl, IMXLPUARTState),
        VMSTATE_UINT32(data, IMXLPUARTState),
        VMSTATE_UINT32(match, IMXLPUARTState),
        VMSTATE_UINT32(modir, IMXLPUARTState),
        VMSTATE_UINT32(fifo, IMXLPUARTState),
        VMSTATE_UINT32(water, IMXLPUARTState),
	VMSTATE_UINT8_ARRAY(rx_fifo, IMXLPUARTState, IMX_LPUART_RX_FIFO_SIZE),
	VMSTATE_UINT32(rx_head, IMXLPUARTState),
	VMSTATE_UINT32(rx_tail, IMXLPUARTState),
	VMSTATE_UINT32(rx_count, IMXLPUARTState),
	VMSTATE_END_OF_LIST()
    },
};

static void imx_lpuart_update_irq(IMXLPUARTState *s)
{
    bool level = false;

    if ((s->ctrl & LPUART_CTRL_TIE) && (s->stat & LPUART_STAT_TDRE)) {
        level = true;
    }

    if ((s->ctrl & LPUART_CTRL_TCIE) && (s->stat & LPUART_STAT_TC)) {
        level = true;
    }

    if ((s->ctrl & LPUART_CTRL_RIE) && (s->stat & LPUART_STAT_RDRF)) {
        level = true;
    }

    qemu_set_irq(s->irq, level);
}

static uint32_t *imx_lpuart_reg_ptr(IMXLPUARTState *s, hwaddr offset)
{
    switch (offset) {
    case LPUART_VERID:
        return &s->verid;
    case LPUART_PARAM:
        return &s->param;
    case LPUART_GLOBAL:
        return &s->global;
    case LPUART_PINCFG:
        return &s->pincfg;
    case LPUART_BAUD:
        return &s->baud;
    case LPUART_STAT:
        return &s->stat;
    case LPUART_CTRL:
        return &s->ctrl;
    case LPUART_DATA:
        return &s->data;
    case LPUART_MATCH:
        return &s->match;
    case LPUART_MODIR:
        return &s->modir;
    case LPUART_FIFO:
        return &s->fifo;
    case LPUART_WATER:
        return &s->water;
    default:
        return NULL;
    }
}


static bool imx_lpuart_rx_full(IMXLPUARTState *s)
{
    return s->rx_count == IMX_LPUART_RX_FIFO_SIZE;
}

static bool imx_lpuart_rx_empty(IMXLPUARTState *s)
{
    return s->rx_count == 0;
}

static void imx_lpuart_rx_push(IMXLPUARTState *s, uint8_t ch)
{
    if (imx_lpuart_rx_full(s)) {
        s->stat |= LPUART_STAT_OR;
        imx_lpuart_update_irq(s);
        return;
    }

    s->rx_fifo[s->rx_tail] = ch;
    s->rx_tail = (s->rx_tail + 1) % IMX_LPUART_RX_FIFO_SIZE;
    s->rx_count++;

    s->stat |= LPUART_STAT_RDRF;
    imx_lpuart_update_irq(s);
}

static uint8_t imx_lpuart_rx_pop(IMXLPUARTState *s)
{
    uint8_t ch = 0;

    if (!imx_lpuart_rx_empty(s)) {
        ch = s->rx_fifo[s->rx_head];
        s->rx_head = (s->rx_head + 1) % IMX_LPUART_RX_FIFO_SIZE;
        s->rx_count--;

        if (s->rx_count == 0) {
            s->stat &= ~LPUART_STAT_RDRF;
        }
    }

    imx_lpuart_update_irq(s);
    return ch;
}

static int imx_lpuart_can_receive(void *opaque)
{
    IMXLPUARTState *s = opaque;

    return IMX_LPUART_RX_FIFO_SIZE - s->rx_count;
}

static void imx_lpuart_receive(void *opaque, const uint8_t *buf, int size)
{
    IMXLPUARTState *s = opaque;

    for (int i = 0; i < size; i++) {
        imx_lpuart_rx_push(s, buf[i]);
    }
}

static void imx_lpuart_event(void *opaque, QEMUChrEvent event)
{
    IMXLPUARTState *s = opaque;

    if (event == CHR_EVENT_BREAK) {
        s->stat |= LPUART_STAT_FE;
        imx_lpuart_rx_push(s, 0);
    }
}

static uint32_t imx_lpuart_fifo_value(IMXLPUARTState *s)
{
    uint32_t value = s->fifo;

    value |= LPUART_FIFO_TXEMPT_MASK;

    if (s->rx_count == 0) {
        value |= LPUART_FIFO_RXEMPT_MASK;
    } else {
        value &= ~LPUART_FIFO_RXEMPT_MASK;
    }

    return value;
}

static uint32_t imx_lpuart_water_value(IMXLPUARTState *s)
{
    uint32_t value = s->water;
    uint32_t rx_count = s->rx_count;

    if (rx_count > 0x1f) {
        rx_count = 0x1f;
    }

    value &= ~(LPUART_WATER_TXCOUNT_MASK | LPUART_WATER_RXCOUNT_MASK);
    value |= LPUART_WATER_RXCOUNT(rx_count);

    return value;
}



static uint64_t imx_lpuart_read(void *opaque, hwaddr offset, unsigned size)
{
    IMXLPUARTState *s = opaque;
    uint32_t *reg;
    uint32_t value;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unsupported read size %u at offset 0x%"
                      HWADDR_PRIx "\n",
                      TYPE_IMX_LPUART, size, offset);
    }

    switch (offset) {
    case LPUART_STAT:
        value = s->stat | LPUART_STAT_TDRE | LPUART_STAT_TC;
	if (s->rx_count > 0) {
		value |= LPUART_STAT_RDRF;
	} else {
	value &= ~LPUART_STAT_RDRF;
	}
        break;
    case LPUART_WATER:
        value = imx_lpuart_water_value(s);
        break;

    case LPUART_FIFO:
	value = imx_lpuart_fifo_value(s);
	break;
    case LPUART_DATA:
	if (!imx_lpuart_rx_empty(s)) {
		value = imx_lpuart_rx_pop(s);
		qemu_chr_fe_accept_input(&s->chr);
	} else {
	value = 0;
	}
	break;

    default:
        reg = imx_lpuart_reg_ptr(s, offset);
        if (reg) {
            value = *reg;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: bad read at offset 0x%" HWADDR_PRIx "\n",
                          TYPE_IMX_LPUART, offset);
            value = 0;
        }
        break;
    }

    return value;
}

static void imx_lpuart_write(void *opaque, hwaddr offset,
                             uint64_t value, unsigned size)
{
    IMXLPUARTState *s = opaque;
    uint32_t *reg;
    uint8_t ch;

    if (size != 4 && offset != LPUART_DATA) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unsupported write size %u at offset 0x%"
                      HWADDR_PRIx ", value 0x%" PRIx64 "\n",
                      TYPE_IMX_LPUART, size, offset, value);
    }

    switch (offset) {
    case LPUART_GLOBAL:
        s->global = value;
        break;
    case LPUART_FIFO:
    {
        if (value & LPUART_FIFO_RXFLUSH_MASK) {
            s->rx_head = 0;
            s->rx_tail = 0;
            s->rx_count = 0;
            s->stat &= ~LPUART_STAT_RDRF;
        }

        if (value & LPUART_FIFO_TXFLUSH_MASK) {
        }

        s->fifo &= ~(value &
                     (LPUART_FIFO_RXUF_MASK | LPUART_FIFO_TXOF_MASK));

        s->fifo =
            (s->fifo & ~(LPUART_FIFO_RXFE_MASK |
                         LPUART_FIFO_TXFE_MASK)) |
            (value & (LPUART_FIFO_RXFE_MASK |
                      LPUART_FIFO_TXFE_MASK));

        imx_lpuart_update_irq(s);
        break;
    }

    case LPUART_STAT:
        s->stat &= ~(value & (LPUART_STAT_PF |
                              LPUART_STAT_FE |
                              LPUART_STAT_NF |
                              LPUART_STAT_OR |
                              LPUART_STAT_IDLE));
        s->stat |= LPUART_STAT_TDRE | LPUART_STAT_TC;
        imx_lpuart_update_irq(s);
        break;

    case LPUART_CTRL:
        s->ctrl = value;
        imx_lpuart_update_irq(s);
	if (s->ctrl & LPUART_CTRL_RE) {
		qemu_chr_fe_accept_input(&s->chr);
	}
        break;

    case LPUART_DATA:
	s->data = value;
        ch = value & 0xff;

        qemu_chr_fe_write_all(&s->chr, &ch, 1);

        s->stat |= LPUART_STAT_TDRE | LPUART_STAT_TC;
        imx_lpuart_update_irq(s);
        break;

    default:
        reg = imx_lpuart_reg_ptr(s, offset);
        if (reg) {
            *reg = value;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: bad write at offset 0x%" HWADDR_PRIx
                          ", value 0x%" PRIx64 "\n",
                          TYPE_IMX_LPUART, offset, value);
        }
        break;
    }
}

static const MemoryRegionOps imx_lpuart_ops = {
    .read = imx_lpuart_read,
    .write = imx_lpuart_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void imx_lpuart_reset(DeviceState *dev)
{
    IMXLPUARTState *s = IMX_LPUART(dev);

    s->verid = 0;
    s->param = 0;
    s->global = 0;
    s->pincfg = 0;
    s->baud = 0;
    s->stat = LPUART_STAT_TDRE | LPUART_STAT_TC;
    s->ctrl = 0;
    s->data = 0;
    s->match = 0;
    s->modir = 0;
    s->fifo = 0;
    s->water = 0;
    s->rx_head = 0;
    s->rx_tail = 0;
    s->rx_count = 0;
    imx_lpuart_update_irq(s);
}

static void imx_lpuart_realize(DeviceState *dev, Error **errp)
{
    IMXLPUARTState *s = IMX_LPUART(dev);

    qemu_chr_fe_set_handlers(&s->chr,imx_lpuart_can_receive,imx_lpuart_receive,
				imx_lpuart_event,NULL,s,NULL,true);
    qemu_chr_fe_accept_input(&s->chr);

}

static void imx_lpuart_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    IMXLPUARTState *s = IMX_LPUART(obj);

    memory_region_init_io(&s->iomem, obj, &imx_lpuart_ops, s,
                          TYPE_IMX_LPUART, IMX_LPUART_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static const Property imx_lpuart_properties[] = {
    DEFINE_PROP_CHR("chardev", IMXLPUARTState, chr),
};

static void imx_lpuart_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = imx_lpuart_realize;
    dc->vmsd = &vmstate_imx_lpuart;
    device_class_set_legacy_reset(dc, imx_lpuart_reset);
    device_class_set_props(dc, imx_lpuart_properties);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    dc->desc = "NXP i.MX LPUART";
}

static const TypeInfo imx_lpuart_info = {
    .name          = TYPE_IMX_LPUART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMXLPUARTState),
    .instance_init = imx_lpuart_init,
    .class_init    = imx_lpuart_class_init,
};

static void imx_lpuart_register_types(void)
{
    type_register_static(&imx_lpuart_info);
}

type_init(imx_lpuart_register_types)
