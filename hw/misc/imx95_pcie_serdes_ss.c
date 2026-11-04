/*
 * NXP i.MX95 PCIe SerDes subsystem (SerDes_SS) registers
 *
 * Registers of the PCIe SerDes subsystem, one instance per PCIe
 * controller. The PHY PLL reports locked as soon as it is enabled and the
 * stream ID look-up table can be read and written.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "system/memory.h"
#include "hw/misc/imx95_pcie_serdes_ss.h"

#define PCIE_PHY_MPLLA_CTRL     0x0010
#define   MPLL_STATE            BIT(30)
#define PE0_LUT_ACSCTRL         0x1008
#define   LUT_ACS_RWA           BIT(16)
#define   LUT_ACS_ENLOC         0x1f
#define PE0_LUT_DATA1           0x100c
#define   LUT_DATA1_MASK        0x80000d3fu  /* VLD [31], DAC_ID [10:8], STREAM_ID [5:0] */
#define PE0_LUT_DATA2           0x1010

typedef struct {
    uint16_t offset;
    uint32_t reset;
    bool ro;
    const char *name;
} Imx95PcieSerdesReg;

static const Imx95PcieSerdesReg imx95_pcie_serdes_regs[] = {
    { 0x0000, 0x2003cc00, false, "PCIE_PHY_GEN_CTRL" },
    { 0x0004, 0x00000000, false, "PCIE_PHY_LPBK_CTRL" },
    { 0x0008, 0x00000002, false, "PCIE_PHY_SRAM_CSR" },
    { 0x000c, 0x00000000, false, "PCIE_PHY_PM_CTRL" },
    { 0x0010, 0x00000000, false, "PCIE_PHY_MPLLA_CTRL" },
    { 0x0014, 0x00000000, false, "PCIE_PHY_MPLLB_CTRL" },
    { 0x0018, 0x00000000, false, "PCIE_PHY_EXT_CTRL_SEL" },
    { 0x001c, 0x00000029, false, "PCIE_PHY_EXT_BS_CTRL" },
    { 0x0020, 0x0000001a, false, "PCIE_PHY_REF_CLK_CTRL" },
    { 0x0030, 0x00010165, false, "PCIE_PHY_EXT_MPLLA_CTRL_1" },
    { 0x0034, 0x00000019, false, "PCIE_PHY_EXT_MPLLA_CTRL_2" },
    { 0x0038, 0x10000000, false, "PCIE_PHY_EXT_MPLLA_CTRL_3" },
    { 0x0040, 0x00040076, false, "PCIE_PHY_EXT_MPLLB_CTRL_1" },
    { 0x0044, 0x04000050, false, "PCIE_PHY_EXT_MPLLB_CTRL_2" },
    { 0x0048, 0x10000000, false, "PCIE_PHY_EXT_MPLLB_CTRL_3" },
    { 0x0050, 0x00e70000, false, "PCIE_PHY_EXT_RX_EQ_CTRL_1A" },
    { 0x0054, 0x00000000, false, "PCIE_PHY_EXT_RX_EQ_CTRL_1B" },
    { 0x0058, 0x00000000, false, "PCIE_PHY_EXT_RX_EQ_CTRL_1C" },
    { 0x0060, 0x00e70000, false, "PCIE_PHY_EXT_RX_EQ_CTRL_2A" },
    { 0x0064, 0x00000000, false, "PCIE_PHY_EXT_RX_EQ_CTRL_2B" },
    { 0x0068, 0x00000000, false, "PCIE_PHY_EXT_RX_EQ_CTRL_2C" },
    { 0x0070, 0x00e7000f, false, "PCIE_PHY_EXT_RX_EQ_CTRL_3A" },
    { 0x0074, 0x00000000, false, "PCIE_PHY_EXT_RX_EQ_CTRL_3B" },
    { 0x0078, 0x00000033, false, "PCIE_PHY_EXT_RX_EQ_CTRL_3C" },
    { 0x0090, 0x001b0546, false, "PCIE_PHY_EXT_CALI_CTRL_1" },
    { 0x0094, 0x001b0546, false, "PCIE_PHY_EXT_CALI_CTRL_2" },
    { 0x0098, 0x00220550, false, "PCIE_PHY_EXT_CALI_CTRL_3" },
    { 0x00a0, 0x51000012, false, "PCIE_PHY_EXT_MISC_CTRL_1" },
    { 0x00a4, 0x820300ff, false, "PCIE_PHY_EXT_MISC_CTRL_2" },
    { 0x00b0, 0x02940044, false, "PCIE_PHY_EXT_TX_EQ_CTRL_1" },
    { 0x00b4, 0x02520066, false, "PCIE_PHY_EXT_TX_EQ_CTRL_2" },
    { 0x00b8, 0x02312255, false, "PCIE_PHY_EXT_TX_EQ_CTRL_3" },
    { 0x00e0, 0x00000002, true , "SS_RO_REG_0" },
    { 0x00e8, 0x00000000, true , "SS_RO_REG_2" },
    { 0x00ec, 0x00000000, true , "SS_RO_REG_3" },
    { 0x00f0, 0x00000000, false, "SS_RW_REG_0" },
    { 0x00f4, 0x00000000, false, "SS_RW_REG_1" },
    { 0x00f8, 0x00000000, false, "SS_RW_REG_2" },
    { 0x00fc, 0x00000000, false, "SS_RW_REG_3" },
    { 0x0100, 0x00000000, false, "SS_RW_REG_4" },
    { 0x0104, 0x00000000, false, "SS_RW_REG_5" },
    { 0x1000, 0x596a0000, true , "PCIE_SUBSYSTEM_VERSION" },
    { 0x1004, 0x00000000, false, "PE0_CLKREQ_CTRL" },
    { 0x1008, 0x00000000, false, "PE0_LUT_ACSCTRL" },
    { 0x100c, 0x00000000, false, "PE0_LUT_DATA1" },
    { 0x1010, 0x00000000, false, "PE0_LUT_DATA2" },
    { 0x1014, 0x00000000, false, "PE0_LUT_INT_STS" },
    { 0x1018, 0x00000000, false, "PE0_LUT_INT_CTRL" },
    { 0x101c, 0x00000000, true , "PE0_LUT_CREQID" },
    { 0x1020, 0x00000000, false, "FLR_CTRL_STS" },
    { 0x1040, 0x00000000, false, "LINK_INT_CTRL_STS" },
    { 0x1050, 0x00000000, false, "PE0_GEN_CTRL_1" },
    { 0x1054, 0x00000000, false, "PE0_GEN_CTRL_2" },
    { 0x1058, 0x00000000, false, "PE0_GEN_CTRL_3" },
    { 0x105c, 0x00000000, false, "PE0_GEN_CTRL_4" },
    { 0x1060, 0x00280000, false, "PE0_PM_CTRL" },
    { 0x1064, 0x00000004, true , "PE0_PM_STS" },
    { 0x1068, 0x00000000, false, "PE0_TX_LTR_MSG_LATENCY" },
    { 0x106c, 0x00000000, false, "PE0_TX_LTR_MSG_FUNC_NUM" },
    { 0x1070, 0x00000000, false, "PE0_TX_MSG_HDR_1" },
    { 0x1074, 0x00000000, false, "PE0_TX_MSG_HDR_2" },
    { 0x1078, 0x00000000, false, "PE0_TX_MSG_HDR_3" },
    { 0x107c, 0x00000000, false, "PE0_TX_MSG_HDR_4" },
    { 0x1080, 0x00000000, false, "PE0_TX_MSG_REQ" },
    { 0x1084, 0x00000000, true , "PE0_RX_SLOT_PWR_PAYLD" },
    { 0x1088, 0x00000000, true , "PE0_RX_CURR_LTR_LATENCY" },
    { 0x1090, 0x00000000, true , "PE0_RX_MSG_HDR_1" },
    { 0x1094, 0x00000000, true , "PE0_RX_MSG_HDR_2" },
    { 0x1098, 0x00000000, true , "PE0_RX_MSG_HDR_3" },
    { 0x109c, 0x00000000, true , "PE0_RX_MSG_HDR_4" },
    { 0x10a0, 0x00000000, false, "PE0_RX_MSG_STS" },
    { 0x10a4, 0x00000000, false, "PE0_RX_MSG_CAP_CTRL" },
    { 0x10a8, 0x00000000, false, "PE0_RX_MSG_INT_CTRL" },
    { 0x10b0, 0x00000000, true , "PE0_LINK_DBG_1" },
    { 0x10b4, 0x01000800, true , "PE0_LINK_DBG_2" },
    { 0x10c0, 0x00000000, true , "PE0_AXI_MSTR_DBG_1" },
    { 0x10c4, 0x00000000, true , "PE0_AXI_MSTR_DBG_2" },
    { 0x10d0, 0x00000000, true , "PE0_AXI_SLV_DBG_1" },
    { 0x10d4, 0x00000000, true , "PE0_AXI_SLV_DBG_2" },
    { 0x10e0, 0x00000000, false, "PE0_ERR_STS" },
    { 0x10e4, 0x00000000, false, "PE0_ERR_INT_CTRL" },
    { 0x10e8, 0x00000000, false, "PE0_INT_STS" },
    { 0x10ec, 0x00000000, false, "PE0_MSI_GEN_CTRL" },
    { 0x10f0, 0x00000000, false, "PE0_FSM_TRACK_1" },
    { 0x10f4, 0x00000000, true , "PE0_FSM_TRACK_2" },
    { 0x3000, 0x00640082, false, "APB_BRIDGE_TO_CTRL" },
    { 0x3008, 0x00000000, false, "PHY_REG_ADDR" },
    { 0x300c, 0x00000000, false, "PHY_REG_DATA" },
    { 0x3010, 0x00000000, false, "RST_CTRL" },
};

static const Imx95PcieSerdesReg *imx95_pcie_serdes_find(hwaddr offset)
{
    for (size_t i = 0; i < ARRAY_SIZE(imx95_pcie_serdes_regs); i++) {
        if (imx95_pcie_serdes_regs[i].offset == offset) {
            return &imx95_pcie_serdes_regs[i];
        }
    }
    return NULL;
}

static uint64_t imx95_pcie_serdes_read(void *opaque, hwaddr offset,
                                       unsigned size)
{
    Imx95PcieSerdesSsState *s = opaque;
    uint32_t value;

    if (!imx95_pcie_serdes_find(offset)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.pcie-serdes-ss: read of undefined register 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }

    value = s->regs[offset / 4];

    /*
     * MPLL lock indicator. The EVK supplies the PHY reference clock (pad or
     * HSIO PLL, RM 158.3), so the PLL is locked as soon as it is asked to be.
     */
    if (offset == PCIE_PHY_MPLLA_CTRL) {
        value |= MPLL_STATE;
    }

    return value;
}

static void imx95_pcie_serdes_write(void *opaque, hwaddr offset,
                                    uint64_t value, unsigned size)
{
    Imx95PcieSerdesSsState *s = opaque;
    const Imx95PcieSerdesReg *r = imx95_pcie_serdes_find(offset);
    uint32_t v = value;

    if (!r) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.pcie-serdes-ss: write to undefined register 0x%"
                      HWADDR_PRIx "\n", offset);
        return;
    }
    if (r->ro) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx95.pcie-serdes-ss: write to read-only register %s\n",
                      r->name);
        return;
    }

    switch (offset) {
    case PCIE_PHY_MPLLA_CTRL:
        v &= ~MPLL_STATE;
        break;
    case PE0_LUT_DATA1:
        v &= LUT_DATA1_MASK;
        break;
    case PE0_LUT_ACSCTRL: {
        unsigned entry = v & LUT_ACS_ENLOC;

        v &= LUT_ACS_RWA | LUT_ACS_ENLOC;
        if (v & LUT_ACS_RWA) {
            /* read the entry into DATA1/DATA2 */
            s->regs[PE0_LUT_DATA1 / 4] = s->lut[entry][0];
            s->regs[PE0_LUT_DATA2 / 4] = s->lut[entry][1];
        } else {
            /* write DATA1/DATA2 into the entry */
            s->lut[entry][0] = s->regs[PE0_LUT_DATA1 / 4];
            s->lut[entry][1] = s->regs[PE0_LUT_DATA2 / 4];
        }
        break;
    }
    default:
        break;
    }

    s->regs[offset / 4] = v;
}

static const MemoryRegionOps imx95_pcie_serdes_ops = {
    .read = imx95_pcie_serdes_read,
    .write = imx95_pcie_serdes_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
};

static void imx95_pcie_serdes_reset(DeviceState *dev)
{
    Imx95PcieSerdesSsState *s = IMX95_PCIE_SERDES_SS(dev);

    memset(s->regs, 0, sizeof(s->regs));
    memset(s->lut, 0, sizeof(s->lut));
    for (size_t i = 0; i < ARRAY_SIZE(imx95_pcie_serdes_regs); i++) {
        s->regs[imx95_pcie_serdes_regs[i].offset / 4] =
            imx95_pcie_serdes_regs[i].reset;
    }
}

static void imx95_pcie_serdes_init(Object *obj)
{
    Imx95PcieSerdesSsState *s = IMX95_PCIE_SERDES_SS(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_pcie_serdes_ops, s,
                          TYPE_IMX95_PCIE_SERDES_SS,
                          IMX95_PCIE_SERDES_SS_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription vmstate_imx95_pcie_serdes = {
    .name = TYPE_IMX95_PCIE_SERDES_SS,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, Imx95PcieSerdesSsState,
                             IMX95_PCIE_SERDES_SS_NUM_REGS),
        VMSTATE_UINT32_2DARRAY(lut, Imx95PcieSerdesSsState,
                               IMX95_PCIE_SERDES_SS_LUT_ENTRIES, 2),
        VMSTATE_END_OF_LIST()
    }
};

static void imx95_pcie_serdes_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, imx95_pcie_serdes_reset);
    dc->vmsd = &vmstate_imx95_pcie_serdes;
    dc->desc = "i.MX95 PCIe SerDes subsystem registers";
}

static const TypeInfo imx95_pcie_serdes_types[] = {
    {
        .name = TYPE_IMX95_PCIE_SERDES_SS,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Imx95PcieSerdesSsState),
        .instance_init = imx95_pcie_serdes_init,
        .class_init = imx95_pcie_serdes_class_init,
    },
};

DEFINE_TYPES(imx95_pcie_serdes_types)
