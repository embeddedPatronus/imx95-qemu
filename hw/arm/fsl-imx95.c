/*
 * NXP i.MX95 SoC emulation
 *
 * Models six Cortex-A55 cores behind a GICv3, the Cortex-M33 System
 * Manager core, the Cortex-M7 core, the memory map and the peripherals.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/main-loop.h"
#include "qapi/error.h"
#include "qobject/qlist.h"
#include "system/address-spaces.h"
#include "system/reset.h"
#include "system/system.h"
#include "hw/arm/bsa.h"
#include "hw/arm/boot.h"
#include "hw/arm/fsl-imx95.h"
#include "hw/core/clock.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/intc/arm_gic_common.h"
#include "hw/intc/arm_gicv3_its_common.h"
#include "hw/misc/unimp.h"
#include "target/arm/gtimer.h"

typedef struct FslImx95MemoryMapEntry {
    hwaddr addr;
    hwaddr size;
    const char *name;
} FslImx95MemoryMapEntry;

static const FslImx95MemoryMapEntry fsl_imx95_memmap[] = {
    [FSL_IMX95_A55_OCRAM] = { 0x20480000, 1 * MiB, "imx95.a55.ocram" },
    [FSL_IMX95_A55_TMPSNS] = { 0x4a440000, 4 * KiB, "imx95.a55-tmpsns" },
    [FSL_IMX95_ADC] = { 0x44530000, 64 * KiB, "imx95.adc" },
    [FSL_IMX95_AON_MU1_A] = { 0x44220000, 64 * KiB, "imx95.aon-mu1-a" },
    [FSL_IMX95_AON_MU1_B] = { 0x44230000, 64 * KiB, "imx95.aon-mu1-b" },
    [FSL_IMX95_AON_MU1_SRAM_A] = { 0x44221000, 4 * KiB, "imx95.aon-mu1-sram" },
    [FSL_IMX95_AON_MU1_SRAM_B] = {
        0x44231000, 4 * KiB, "imx95.aon-mu1-sram-b-alias"
    },
    [FSL_IMX95_AON_MU2_A] = { 0x445b0000, 64 * KiB, "imx95.aon-mu2-a" },
    [FSL_IMX95_AON_MU2_B] = { 0x445c0000, 64 * KiB, "imx95.aon-mu2-b" },
    [FSL_IMX95_AON_MU2_SRAM_A] = { 0x445b1000, 1 * KiB, "imx95.aon-mu2-sram" },
    [FSL_IMX95_AON_MU2_SRAM_B] = {
        0x445c1000, 1 * KiB, "imx95.aon-mu2-sram-b-alias"
    },
    [FSL_IMX95_AON_MU5_A] = { 0x44610000, 64 * KiB, "imx95.aon-mu5-a" },
    [FSL_IMX95_AON_MU5_B] = { 0x44620000, 64 * KiB, "imx95.aon-mu5-b" },
    [FSL_IMX95_AON_MU5_SRAM_A] = { 0x44611000, 4 * KiB, "imx95.aon-mu5-sram" },
    [FSL_IMX95_AON_MU5_SRAM_B] = {
        0x44621000, 4 * KiB, "imx95.aon-mu5-sram-b-alias"
    },
    [FSL_IMX95_BBNSM] = { 0x44440000, 64 * KiB, "imx95.bbnsm" },
    [FSL_IMX95_BLK_CTRL_GPUMIX] = {
        0x4d810000, 64 * KiB, "imx95.blk-ctrl-gpumix"
    },
    [FSL_IMX95_BLK_CTRL_S_AONMIX] = {
        0x444f0000, 64 * KiB, "imx95.blk-ctrl-s-aonmix"
    },
    [FSL_IMX95_BLK_CTRL_WAKEUPMIX1] = {
        0x42420000, 64 * KiB, "imx95.blk-ctrl-wakeupmix1"
    },
    [FSL_IMX95_CAMERA_CSR] = { 0x4ac10000, 64 * KiB, "imx95.camera-csr" },
    [FSL_IMX95_CAMERA_NOC] = { 0x4aff0000, 64 * KiB, "imx95.camera-noc" },
    [FSL_IMX95_CCM] = { 0x44450000, 64 * KiB, "imx95.ccm" },
    [FSL_IMX95_DDRC] = { 0x4e080000, 128 * KiB, "imx95.ddrc" },
    [FSL_IMX95_DDRMIX] = { 0x5d000000, 32 * MiB, "imx95.ddrmix" },
    [FSL_IMX95_DISPLAYMIX_IRQSTEER] = {
        0x4b0b0000, 4 * KiB, "imx95.displaymix-irqsteer"
    },
    [FSL_IMX95_EDMA1] = { 0x44000000, 2112 * KiB, "imx95.edma1" },
    [FSL_IMX95_EDMA2] = { 0x42000000, 2112 * KiB, "imx95.edma2" },
    [FSL_IMX95_EDMA3] = { 0x42210000, 2112 * KiB, "imx95.edma3" },
    [FSL_IMX95_ELE_MU] = { 0x47500000, 512 * KiB, "imx95.ele-mu" },
    [FSL_IMX95_FSB] = { 0x47510000, 64 * KiB, "imx95.fsb" },
    [FSL_IMX95_ERM] = { 0x44560000, 64 * KiB, "imx95.erm" },
    [FSL_IMX95_FRACTPLL] = { 0x44481000, 4 * KiB, "imx95.fractpll" },
    [FSL_IMX95_FRO_TUNER] = { 0x44485000, 64 * KiB, "imx95.fro-tuner" },
    [FSL_IMX95_FSPI1_MMAP] = { 0x28000000, 128 * MiB, "imx95.fspi1-mmap" },
    [FSL_IMX95_FSPI1_REG] = { 0x425e0000, 64 * KiB, "imx95.fspi1-reg" },
    [FSL_IMX95_GIC_DIST] = { 0x48000000, 64 * KiB, "gic_dist" },
    [FSL_IMX95_GIC_ITS] = { 0x48040000, 128 * KiB, "gic_its" },
    [FSL_IMX95_GIC_REDIST] = { 0x48060000, 768 * KiB, "gic_redist" },
    [FSL_IMX95_GPC_CPU_CTRL] = { 0x44470000, 18 * KiB, "imx95.gpc-cpu-ctrl" },
    [FSL_IMX95_GPC_GLOBAL] = { 0x44474800, 4 * KiB, "imx95.gpc-global" },
    [FSL_IMX95_GPIO1] = { 0x47400000, 64 * KiB, "imx95.gpio1" },
    [FSL_IMX95_GPIO2] = { 0x43810000, 64 * KiB, "imx95.gpio2" },
    [FSL_IMX95_GPIO3] = { 0x43820000, 64 * KiB, "imx95.gpio3" },
    [FSL_IMX95_GPIO4] = { 0x43840000, 64 * KiB, "imx95.gpio4" },
    [FSL_IMX95_GPIO5] = { 0x43850000, 64 * KiB, "imx95.gpio5" },
    [FSL_IMX95_HSIO_BLK_CTRL] = { 0x4c010000, 64 * KiB, "imx95.hsio-blk-ctrl" },
    [FSL_IMX95_LPI2C1] = { 0x44340000, 64 * KiB, "imx95.lpi2c1" },
    [FSL_IMX95_LPI2C2] = { 0x44350000, 64 * KiB, "imx95.lpi2c2" },
    [FSL_IMX95_LPI2C3] = { 0x42530000, 64 * KiB, "imx95.lpi2c3" },
    [FSL_IMX95_LPI2C4] = { 0x42540000, 64 * KiB, "imx95.lpi2c4" },
    [FSL_IMX95_LPI2C5] = { 0x426b0000, 64 * KiB, "imx95.lpi2c5" },
    [FSL_IMX95_LPI2C6] = { 0x426c0000, 64 * KiB, "imx95.lpi2c6" },
    [FSL_IMX95_LPI2C7] = { 0x426d0000, 64 * KiB, "imx95.lpi2c7" },
    [FSL_IMX95_LPI2C8] = { 0x426e0000, 64 * KiB, "imx95.lpi2c8" },
    [FSL_IMX95_LPUART_A55] = { 0x44380000, 64 * KiB, "imx95.lpuart-a55" },
    [FSL_IMX95_LPUART_A55_ALIAS] = {
        0x5a060000, 64 * KiB, "imx8-lpuart6-alias"
    },
    [FSL_IMX95_LPUART_SM] = { 0x44390000, 64 * KiB, "imx95.lpuart-sm" },
    [FSL_IMX95_LPUART_M7] = { 0x42570000, 64 * KiB, "imx95.lpuart-m7" },
    [FSL_IMX95_M33_CODE_TCM] = { 0x1ffc0000, 256 * KiB, "imx95.m33.code-tcm" },
    [FSL_IMX95_M33_DATA_TCM] = { 0x20000000, 256 * KiB, "imx95.m33.data-tcm" },
    [FSL_IMX95_M33_XCACHE_LPCAC] = {
        0x44400000, 64 * KiB, "imx95.m33-xcache-lpcac"
    },
    [FSL_IMX95_MU3] = { 0x445d0000, 64 * KiB, "imx95.mu3" },
    [FSL_IMX95_MU4] = { 0x445f0000, 64 * KiB, "imx95.mu4" },
    [FSL_IMX95_MU6] = { 0x44630000, 64 * KiB, "imx95.mu6" },
    [FSL_IMX95_MU8] = { 0x42730000, 64 * KiB, "imx95.mu8" },
    [FSL_IMX95_NETCMIX_CTRL] = { 0x4c810000, 64 * KiB, "imx95.netcmix-ctrl" },
    [FSL_IMX95_NETC_ECAM0] = { 0x4ca00000, 1 * MiB, "imx95.netc-ecam0" },
    [FSL_IMX95_NETC_ECAM1] = { 0x4cb00000, 1 * MiB, "imx95.netc-ecam1" },
    [FSL_IMX95_NETC_IERB] = { 0x4cde0000, 64 * KiB, "imx95.netc-ierb" },
    [FSL_IMX95_NETC_PRB] = { 0x4cdf0000, 64 * KiB, "imx95.netc-prb" },
    [FSL_IMX95_NOCMIX_BLK_CTRL] = {
        0x49000000, 64 * KiB, "imx95.nocmix-blk-ctrl"
    },
    [FSL_IMX95_NOC_CAMERA_BLK_CTRL] = {
        0x49062000, 64 * KiB, "imx95.noc-camera-blk-ctrl"
    },
    [FSL_IMX95_NOC_SRAMCTL] = { 0x490a0000, 64 * KiB, "imx95.noc-sramctl" },
    [FSL_IMX95_OSC24M] = { 0x44480000, 4 * KiB, "imx95.osc24m" },
    [FSL_IMX95_PCIE0] = { 0x4c300000, 64 * KiB, "imx95.pcie0" },
    [FSL_IMX95_PCIE0_APP] = { 0x4c340000, 16 * KiB, "imx95.pcie0-app" },
    [FSL_IMX95_PCIE0_ATU] = { 0x4c360000, 64 * KiB, "imx95.pcie0-atu" },
    [FSL_IMX95_PCIE1] = { 0x4c380000, 64 * KiB, "imx95.pcie1" },
    [FSL_IMX95_PCIE1_APP] = { 0x4c3c0000, 16 * KiB, "imx95.pcie1-app" },
    [FSL_IMX95_PCIE1_ATU] = { 0x4c3e0000, 64 * KiB, "imx95.pcie1-atu" },
    [FSL_IMX95_PERIPH_CATCHALL] = {
        0x40000000, 256 * MiB, "imx95.periph-catchall"
    },
    [FSL_IMX95_RAM] = { 0x80000000, 16 * GiB, "ram" },
    [FSL_IMX95_SAR_ADC] = { 0x443c0000, 64 * KiB, "imx95.sar-adc" },
    [FSL_IMX95_SC_MU] = { 0x5d1c0000, 64 * KiB, "imx95.sc-mu" },
    [FSL_IMX95_SMMUV3] = { 0x490d0000, 128 * KiB, "imx95.smmuv3-global" },
    [FSL_IMX95_SRC_GEN] = { 0x44460000, 64 * KiB, "imx95.src-gen" },
    [FSL_IMX95_SYSCNT_CTRL] = { 0x44290000, 64 * KiB, "imx95.sysctr-control" },
    [FSL_IMX95_SYSCNT_CMP] = { 0x442a0000, 64 * KiB, "imx95.sysctr-compare" },
    [FSL_IMX95_SYSCNT_READ] = { 0x442b0000, 64 * KiB, "imx95.sysctr-read" },
    [FSL_IMX95_TEMPSENSE] = { 0x44210000, 64 * KiB, "imx95.tempsense" },
    [FSL_IMX95_TMPSNS] = { 0x44482000, 4 * KiB, "imx95.tmpsns" },
    [FSL_IMX95_TPM3] = { 0x424e0000, 64 * KiB, "imx95.tpm3-pwm" },
    [FSL_IMX95_TPM6] = { 0x42510000, 64 * KiB, "imx95.tpm6-pwm" },
    [FSL_IMX95_TRDC_AON] = { 0x44270000, 128 * KiB, "imx95.trdc-aon" },
    [FSL_IMX95_TRDC_CAMERA] = { 0x4ac40000, 128 * KiB, "imx95.trdc-camera" },
    [FSL_IMX95_TRDC_DISPLAY] = { 0x4b040000, 128 * KiB, "imx95.trdc-display" },
    [FSL_IMX95_TRDC_GPU] = { 0x4d840000, 128 * KiB, "imx95.trdc-gpu" },
    [FSL_IMX95_TRDC_HSIO] = { 0x4c040000, 128 * KiB, "imx95.trdc-hsio" },
    [FSL_IMX95_TRDC_MEGA] = { 0x42810000, 128 * KiB, "imx95.trdc-mega" },
    [FSL_IMX95_TRDC_NETC] = { 0x4c840000, 128 * KiB, "imx95.trdc-netc" },
    [FSL_IMX95_TRDC_NOC] = { 0x49010000, 128 * KiB, "imx95.trdc-noc" },
    [FSL_IMX95_TRDC_VPU] = { 0x4c440000, 128 * KiB, "imx95.trdc-vpu" },
    [FSL_IMX95_TRDC_WAKEUP] = { 0x42460000, 128 * KiB, "imx95.trdc-wakeup" },
    [FSL_IMX95_USB1] = { 0x4c100000, 64 * KiB, "imx95.usb1" },
    [FSL_IMX95_USB2] = { 0x4c200000, 4 * KiB, "imx95.usb2" },
    [FSL_IMX95_USB_CTRL] = { 0x4c1f0000, 64 * KiB, "imx95.usb-ctrl" },
    [FSL_IMX95_USDHC1] = { 0x42850000, 64 * KiB, "imx95.usdhc1" },
    [FSL_IMX95_USDHC2] = { 0x42860000, 64 * KiB, "imx95.usdhc2" },
    [FSL_IMX95_USDHC3] = { 0x428B0000, 64 * KiB, "imx95.usdhc3" },
    [FSL_IMX95_V2X_DBG_MU] = { 0x47350000, 64 * KiB, "imx95.v2x-dbg-mu" },
    [FSL_IMX95_V2X_MU0] = { 0x47300000, 64 * KiB, "imx95.v2x-mu0" },
    [FSL_IMX95_V2X_MU3] = { 0x47330000, 64 * KiB, "imx95.v2x-mu3" },
    [FSL_IMX95_V2X_MU4] = { 0x47340000, 64 * KiB, "imx95.v2x-mu4" },
    [FSL_IMX95_V2X_MU6] = { 0x47320000, 64 * KiB, "imx95.v2x-mu6" },
    [FSL_IMX95_VFCCU_FHID0] = { 0x446b0000, 64 * KiB, "imx95.vfccu-fhid0" },
    [FSL_IMX95_VFCCU_GLOBAL] = { 0x44570000, 64 * KiB, "imx95.vfccu-global" },
    [FSL_IMX95_WAKEUP_MU] = { 0x42430000, 64 * KiB, "imx95.wakeup-mu" },
    [FSL_IMX95_WAKEUP_MU_B] = { 0x42440000, 64 * KiB, "imx95.wakeup-mu-b" },
    [FSL_IMX95_WDOG1] = { 0x442d0000, 64 * KiB, "imx95.wdog1" },
    [FSL_IMX95_WDOG2] = { 0x442e0000, 64 * KiB, "imx95.wdog2" },
    [FSL_IMX95_WDOG4] = { 0x42490000, 64 * KiB, "imx95.wdog4" },
    [FSL_IMX95_WDOG5] = { 0x424a0000, 64 * KiB, "imx95.wdog5" },
};

typedef struct FslImx95SimpleDeviceConfig {
    unsigned int region;
    int irq;
    int mmio_priority;
} FslImx95SimpleDeviceConfig;

#define FSL_IMX95_NO_IRQ (-1)
#define FSL_IMX95_NO_OVERLAP (-1)

static const FslImx95SimpleDeviceConfig fsl_imx95_gpio_cfg[] = {
    { FSL_IMX95_GPIO1, FSL_IMX95_NO_IRQ, FSL_IMX95_NO_OVERLAP },
    { FSL_IMX95_GPIO2, FSL_IMX95_NO_IRQ, FSL_IMX95_NO_OVERLAP },
    { FSL_IMX95_GPIO3, FSL_IMX95_NO_IRQ, FSL_IMX95_NO_OVERLAP },
    { FSL_IMX95_GPIO4, FSL_IMX95_NO_IRQ, FSL_IMX95_NO_OVERLAP },
    { FSL_IMX95_GPIO5, FSL_IMX95_NO_IRQ, FSL_IMX95_NO_OVERLAP },
};

static const FslImx95SimpleDeviceConfig fsl_imx95_wdog_cfg[] = {
    { FSL_IMX95_WDOG1, FSL_IMX95_NO_IRQ, FSL_IMX95_NO_OVERLAP },
    { FSL_IMX95_WDOG2, FSL_IMX95_NO_IRQ, FSL_IMX95_NO_OVERLAP },
    { FSL_IMX95_WDOG4, FSL_IMX95_NO_IRQ, FSL_IMX95_NO_OVERLAP },
    { FSL_IMX95_WDOG5, FSL_IMX95_NO_IRQ, FSL_IMX95_NO_OVERLAP },
};

typedef struct FslImx95MuConfig {
    unsigned int a_region;
    unsigned int b_region;
    unsigned int sram_a_region;
    unsigned int sram_b_region;
    unsigned int mua_irq;
    unsigned int mub_irq;
} FslImx95MuConfig;

typedef struct FslImx95LpuartConfig {
    unsigned int region;
    unsigned int serial;
    unsigned int irq;
    bool to_gic;
} FslImx95LpuartConfig;

typedef struct FslImx95EdmaConfig {
    unsigned int region;
    unsigned int version;
    unsigned int channels;
    unsigned int requests;
    unsigned int irq_lines;
    unsigned int irq_base;
    unsigned int err_irq;
} FslImx95EdmaConfig;

typedef struct FslImx95Lpi2cConfig {
    unsigned int region;
    unsigned int irq;
    unsigned int edma;
    unsigned int tx_request;
    unsigned int rx_request;
} FslImx95Lpi2cConfig;

typedef struct FslImx95UsdhcConfig {
    unsigned int region;
    unsigned int irq;
} FslImx95UsdhcConfig;

typedef struct FslImx95PcieConfig {
    unsigned int dbi_region;
    unsigned int atu_region;
    unsigned int inta_irq;
    unsigned int msi_irq;
} FslImx95PcieConfig;

static const FslImx95MuConfig fsl_imx95_aon_mu_cfg[] = {
    {
        FSL_IMX95_AON_MU1_A, FSL_IMX95_AON_MU1_B,
        FSL_IMX95_AON_MU1_SRAM_A, FSL_IMX95_AON_MU1_SRAM_B,
        FSL_IMX95_AON_MU1_MUA_IRQ, FSL_IMX95_AON_MU1_MUB_M33_IRQ,
    }, {
        FSL_IMX95_AON_MU2_A, FSL_IMX95_AON_MU2_B,
        FSL_IMX95_AON_MU2_SRAM_A, FSL_IMX95_AON_MU2_SRAM_B,
        FSL_IMX95_AON_MU2_MUA_IRQ, FSL_IMX95_AON_MU2_MUB_M33_IRQ,
    },
};

static const FslImx95LpuartConfig fsl_imx95_lpuart_cfg[] = {
    { FSL_IMX95_LPUART_SM, 0, FSL_IMX95_LPUART_SM_IRQ, false },
    { FSL_IMX95_LPUART_A55, 1, FSL_IMX95_LPUART_A55_IRQ, true },
};

static const FslImx95EdmaConfig fsl_imx95_edma_cfg[] = {
    [FSL_IMX95_EDMA_AON] = {
        FSL_IMX95_EDMA1, IMX_EDMA_V3, 32, 32, 31,
        FSL_IMX95_EDMA_AON_IRQ, FSL_IMX95_EDMA_AON_ERR_IRQ,
    },
    [FSL_IMX95_EDMA_WAKEUP2] = {
        FSL_IMX95_EDMA2, IMX_EDMA_V5, 64, 128, 32,
        FSL_IMX95_EDMA_WAKEUP2_IRQ, FSL_IMX95_EDMA_WAKEUP2_ERR_IRQ,
    },
    [FSL_IMX95_EDMA_WAKEUP3] = {
        FSL_IMX95_EDMA3, IMX_EDMA_V5, 64, 128, 32,
        FSL_IMX95_EDMA_WAKEUP3_IRQ, FSL_IMX95_EDMA_WAKEUP3_ERR_IRQ,
    },
};

static const FslImx95Lpi2cConfig fsl_imx95_lpi2c_cfg[] = {
    {
        FSL_IMX95_LPI2C1, FSL_IMX95_LPI2C1_IRQ, FSL_IMX95_EDMA_AON,
        FSL_IMX95_DMAREQ_LPI2C1_TX, FSL_IMX95_DMAREQ_LPI2C1_RX,
    }, {
        FSL_IMX95_LPI2C2, FSL_IMX95_LPI2C2_IRQ, FSL_IMX95_EDMA_AON,
        FSL_IMX95_DMAREQ_LPI2C2_TX, FSL_IMX95_DMAREQ_LPI2C2_RX,
    }, {
        FSL_IMX95_LPI2C3, FSL_IMX95_LPI2C3_IRQ, FSL_IMX95_EDMA_WAKEUP2,
        FSL_IMX95_DMAREQ_LPI2C3_TX, FSL_IMX95_DMAREQ_LPI2C3_RX,
    }, {
        FSL_IMX95_LPI2C4, FSL_IMX95_LPI2C4_IRQ, FSL_IMX95_EDMA_WAKEUP2,
        FSL_IMX95_DMAREQ_LPI2C4_TX, FSL_IMX95_DMAREQ_LPI2C4_RX,
    }, {
        FSL_IMX95_LPI2C5, FSL_IMX95_LPI2C5_IRQ, FSL_IMX95_EDMA_WAKEUP2,
        FSL_IMX95_DMAREQ_LPI2C5_TX, FSL_IMX95_DMAREQ_LPI2C5_RX,
    }, {
        FSL_IMX95_LPI2C6, FSL_IMX95_LPI2C6_IRQ, FSL_IMX95_EDMA_WAKEUP2,
        FSL_IMX95_DMAREQ_LPI2C6_TX, FSL_IMX95_DMAREQ_LPI2C6_RX,
    }, {
        FSL_IMX95_LPI2C7, FSL_IMX95_LPI2C7_IRQ, FSL_IMX95_EDMA_WAKEUP2,
        FSL_IMX95_DMAREQ_LPI2C7_TX, FSL_IMX95_DMAREQ_LPI2C7_RX,
    }, {
        FSL_IMX95_LPI2C8, FSL_IMX95_LPI2C8_IRQ, FSL_IMX95_EDMA_WAKEUP2,
        FSL_IMX95_DMAREQ_LPI2C8_TX, FSL_IMX95_DMAREQ_LPI2C8_RX,
    },
};

static const FslImx95UsdhcConfig fsl_imx95_usdhc_cfg[] = {
    { FSL_IMX95_USDHC1, FSL_IMX95_USDHC1_IRQ },
    { FSL_IMX95_USDHC2, FSL_IMX95_USDHC2_IRQ },
    { FSL_IMX95_USDHC3, FSL_IMX95_USDHC3_IRQ },
};

static const FslImx95PcieConfig fsl_imx95_pcie_cfg[] = {
    {
        FSL_IMX95_PCIE0, FSL_IMX95_PCIE0_ATU,
        FSL_IMX95_PCIE0_INTA_IRQ, FSL_IMX95_PCIE0_MSI_IRQ,
    }, {
        FSL_IMX95_PCIE1, FSL_IMX95_PCIE1_ATU,
        FSL_IMX95_PCIE1_INTA_IRQ, FSL_IMX95_PCIE1_MSI_IRQ,
    },
};

static const unsigned int fsl_imx95_serdes_region[] = {
    FSL_IMX95_PCIE0_APP,
    FSL_IMX95_PCIE1_APP,
};

static const unsigned int fsl_imx95_netc_ecam_region[] = {
    FSL_IMX95_NETC_ECAM0,
    FSL_IMX95_NETC_ECAM1,
};

QEMU_BUILD_BUG_ON(ARRAY_SIZE(fsl_imx95_aon_mu_cfg) !=
                  FSL_IMX95_NUM_AON_MUS);
QEMU_BUILD_BUG_ON(ARRAY_SIZE(fsl_imx95_lpuart_cfg) !=
                  FSL_IMX95_NUM_LPUARTS);
QEMU_BUILD_BUG_ON(ARRAY_SIZE(fsl_imx95_edma_cfg) != FSL_IMX95_NUM_EDMAS);
QEMU_BUILD_BUG_ON(ARRAY_SIZE(fsl_imx95_lpi2c_cfg) != FSL_IMX95_NUM_LPI2CS);
QEMU_BUILD_BUG_ON(ARRAY_SIZE(fsl_imx95_usdhc_cfg) != FSL_IMX95_NUM_USDHCS);
QEMU_BUILD_BUG_ON(ARRAY_SIZE(fsl_imx95_pcie_cfg) !=
                  FSL_IMX95_NUM_PCIES);
QEMU_BUILD_BUG_ON(ARRAY_SIZE(fsl_imx95_serdes_region) !=
                  FSL_IMX95_NUM_PCIES);
QEMU_BUILD_BUG_ON(ARRAY_SIZE(fsl_imx95_netc_ecam_region) !=
                  FSL_IMX95_NUM_PCIES);

static bool fsl_imx95_realize_simple_device(SysBusDevice *sbd,
                                            const FslImx95SimpleDeviceConfig *cfg,
                                            DeviceState *intc,
                                            Error **errp)
{
    if (!sysbus_realize(sbd, errp)) {
        return false;
    }

    if (cfg->mmio_priority == FSL_IMX95_NO_OVERLAP) {
        sysbus_mmio_map(sbd, 0, fsl_imx95_memmap[cfg->region].addr);
    } else {
        sysbus_mmio_map_overlap(sbd, 0,
                                fsl_imx95_memmap[cfg->region].addr,
                                cfg->mmio_priority);
    }

    if (cfg->irq != FSL_IMX95_NO_IRQ) {
        sysbus_connect_irq(sbd, 0, qdev_get_gpio_in(intc, cfg->irq));
    }

    return true;
}

static void fsl_imx95_init(Object *obj)
{
    FslImx95State *s = FSL_IMX95(obj);
    int i;

    object_initialize_child(obj, "a55-cluster", &s->a55_cluster,
                            TYPE_CPU_CLUSTER);
    for (i = 0; i < FSL_IMX95_NUM_CPUS; i++) {
        g_autofree char *name = g_strdup_printf("cpu%d", i);

        object_initialize_child(OBJECT(&s->a55_cluster), name, &s->cpu[i],
                                ARM_CPU_TYPE_NAME("cortex-a55"));
    }
    object_initialize_child(obj, "gic", &s->gic, gicv3_class_name());
    object_initialize_child(obj, "gic-its", &s->its, its_class_name());
    object_initialize_child(obj, "m33", &s->m33, TYPE_ARMV7M);
    object_initialize_child(obj, "m7", &s->m7, TYPE_ARMV7M);

    s->m33_cpuclk = clock_new(obj, "m33-cpuclk");
    s->m33_refclk = clock_new(obj, "m33-refclk");
    s->m7_cpuclk = clock_new(obj, "m7-cpuclk");
    s->m7_refclk = clock_new(obj, "m7-refclk");
    object_initialize_child(obj, "m7-lpuart", &s->m7_lpuart, TYPE_IMX_LPUART);

    for (i = 0; i < ARRAY_SIZE(s->aon_mu); i++) {
        g_autofree char *name = g_strdup_printf("aon-mu%d", i + 1);
        object_initialize_child(obj, name, &s->aon_mu[i], TYPE_IMX95_MU);
    }
    object_initialize_child(obj, "wakeup-mu", &s->wakeup_mu, TYPE_IMX95_MU);
    object_initialize_child(obj, "m7-mu", &s->m7_mu, TYPE_IMX95_MU);

    for (i = 0; i < FSL_IMX95_NUM_LPUARTS; i++) {
        g_autofree char *name = g_strdup_printf("lpuart%d", i + 1);
        object_initialize_child(obj, name, &s->lpuart[i], TYPE_IMX_LPUART);
    }

    for (i = 0; i < ARRAY_SIZE(s->edma); i++) {
        g_autofree char *name = g_strdup_printf("edma%d", i + 1);
        object_initialize_child(obj, name, &s->edma[i], TYPE_IMX_EDMA);
    }

    for (i = 0; i < FSL_IMX95_NUM_LPI2CS; i++) {
        g_autofree char *name = g_strdup_printf("lpi2c%d", i + 1);
        object_initialize_child(obj, name, &s->lpi2c[i], TYPE_IMX_LPI2C);
    }

    for (i = 0; i < FSL_IMX95_NUM_USDHCS; i++) {
        g_autofree char *name = g_strdup_printf("usdhc%d", i + 1);
        object_initialize_child(obj, name, &s->usdhc[i], TYPE_IMX_USDHC);
    }

    object_initialize_child(obj, "usb1", &s->usb1, TYPE_USB_DWC3);
    object_initialize_child(obj, "usb2", &s->usb2, TYPE_CHIPIDEA);
    object_initialize_child(obj, "ccm", &s->ccm, TYPE_IMX95_CCM);
    object_initialize_child(obj, "ddrc", &s->ddrc, TYPE_IMX95_DDRC);
    object_initialize_child(obj, "sysctr", &s->sysctr, TYPE_IMX95_SYSCTR);
    for (i = 0; i < ARRAY_SIZE(s->pcie_serdes); i++) {
        g_autofree char *serdes_name = g_strdup_printf("pcie-serdes%d", i);
        g_autofree char *ecam_name = g_strdup_printf("netc-ecam%d", i);

        object_initialize_child(obj, serdes_name, &s->pcie_serdes[i],
                                TYPE_IMX95_PCIE_SERDES_SS);
        object_initialize_child(obj, ecam_name, &s->netc_ecam[i],
                                TYPE_IMX95_NETC_ECAM);
    }
    object_initialize_child(obj, "hsio-blk-ctrl", &s->hsio_blk_ctrl,
                            TYPE_IMX95_HSIO_BLK_CTRL);
    object_initialize_child(obj, "camera-csr", &s->camera_csr,
                            TYPE_IMX95_CAMERA_CSR);
    object_initialize_child(obj, "camera-noc", &s->camera_noc,
                            TYPE_IMX95_CAMERA_NOC);
    object_initialize_child(obj, "bbnsm", &s->bbnsm, TYPE_IMX95_BBNSM);
    object_initialize_child(obj, "fractpll", &s->fractpll, TYPE_IMX95_FRACTPLL);
    object_initialize_child(obj, "a55-tmpsns", &s->tmpsns, TYPE_IMX95_TMPSNS);
    object_initialize_child(obj, "smmuv3", &s->smmuv3, TYPE_IMX95_SMMUV3);
    for (i = 0; i < IMX95_SMMU_PMCG_NUM; i++) {
        g_autofree char *name = g_strdup_printf("smmu-pmcg%d", i);

        object_initialize_child(obj, name, &s->smmu_pmcg[i],
                                TYPE_IMX95_SMMU_PMCG);
    }
    object_initialize_child(obj, "fspi1", &s->fspi, TYPE_IMX95_FSPI);
    object_initialize_child(obj, "src-gen", &s->src_gen, TYPE_IMX95_SRC_GEN);
    object_initialize_child(obj, "adc", &s->adc, TYPE_IMX95_ADC);
    object_initialize_child(obj, "gpio1", &s->gpio[0], TYPE_IMX95_GPIO);
    object_initialize_child(obj, "gpio2", &s->gpio[1], TYPE_IMX95_GPIO);
    object_initialize_child(obj, "gpio3", &s->gpio[2], TYPE_IMX95_GPIO);
    object_initialize_child(obj, "gpio4", &s->gpio[3], TYPE_IMX95_GPIO);
    object_initialize_child(obj, "gpio5", &s->gpio[4], TYPE_IMX95_GPIO);
    object_initialize_child(obj, "wdog1", &s->wdog[0], TYPE_IMX95_WDOG32);
    object_initialize_child(obj, "wdog2", &s->wdog[1], TYPE_IMX95_WDOG32);
    object_initialize_child(obj, "wdog4", &s->wdog[2], TYPE_IMX95_WDOG32);
    object_initialize_child(obj, "wdog5", &s->wdog[3], TYPE_IMX95_WDOG32);
    object_initialize_child(obj, "tpm3", &s->tpm[0], TYPE_IMX95_TPM_PWM);
    object_initialize_child(obj, "tpm6", &s->tpm[1], TYPE_IMX95_TPM_PWM);
    object_initialize_child(obj, "blk-ctrl-s-aonmix", &s->blk_ctrl_aonmix,
                            TYPE_IMX95_BLK_CTRL_S_AONMIX);
    object_initialize_child(obj, "ele-mu", &s->ele_mu, TYPE_IMX95_ELE_MU);
    object_initialize_child(obj, "v2x-dbg-mu", &s->v2x_dbg_mu,
                            TYPE_IMX95_V2X_DBG_MU);
    object_initialize_child(obj, "sc-mu", &s->sc_mu, TYPE_IMX95_SC_MU);
    for (i = 0; i < ARRAY_SIZE(s->pcie); i++) {
        g_autofree char *nm = g_strdup_printf("pcie%d", i);

        object_initialize_child(obj, nm, &s->pcie[i], TYPE_IMX95_PCIE_HOST);
    }
}

static void fsl_imx95_m7_start_work(CPUState *cs, run_on_cpu_data data)
{
    ARMCPU *cpu = ARM_CPU(cs);

    cpu->init_svtor = data.host_int;
    cpu->init_nsvtor = data.host_int;
    cs->start_powered_off = false;
    cpu_reset(cs);
    cs->start_powered_off = true;
    cs->halted = 0;
}

static void fsl_imx95_m7_stop_work(CPUState *cs, run_on_cpu_data data)
{
    cs->halted = 1;
    cs->exception_index = EXCP_HLT;
}

/* Run level from the AONMIX: start the M7 at INITVTOR, or hold it */
static void fsl_imx95_m7_run_in(void *opaque, int n, int level)
{
    FslImx95State *s = opaque;
    CPUState *cs = CPU(s->m7.cpu);

    if (level) {
        uint32_t vtor = imx95_blk_ctrl_s_aonmix_get_m7_initvtor(
            &s->blk_ctrl_aonmix);

        async_run_on_cpu(cs, fsl_imx95_m7_start_work,
                         RUN_ON_CPU_HOST_INT(vtor));
    } else {
        async_run_on_cpu(cs, fsl_imx95_m7_stop_work, RUN_ON_CPU_NULL);
    }
}

#define FSL_IMX95_M7_TCM_SYS_ADDR   0x203c0000
#define FSL_IMX95_M7_TCM_SEC_ADDR   0x303c0000
#define FSL_IMX95_M7_ITCM_SIZE      (256 * KiB)
#define FSL_IMX95_M7_DTCM_SIZE      (256 * KiB)
#define FSL_IMX95_M7_DTCM_BASE      0x20000000
#define FSL_IMX95_M7_NUM_IRQS       240
#define FSL_IMX95_SMMU_PMCG0_BASE   0x490d2000
#define FSL_IMX95_SMMU_PMCG1_BASE   0x49112000
#define FSL_IMX95_SMMU_PMCG_STRIDE  0x00020000

static bool fsl_imx95_realize_core(FslImx95State *s, DeviceState *dev,
                                    MemoryRegion *sysmem,
                                    DeviceState *gicdev,
                                    DeviceState *m33dev,
                                    Error **errp)
{
    int i;

    /* On-chip memories */
    if (!memory_region_init_ram(&s->m33_code_tcm, OBJECT(dev),
                                fsl_imx95_memmap[FSL_IMX95_M33_CODE_TCM].name,
                                fsl_imx95_memmap[FSL_IMX95_M33_CODE_TCM].size,
                                errp)) {
        return false;
    }
    memory_region_add_subregion(sysmem,
                                fsl_imx95_memmap[FSL_IMX95_M33_CODE_TCM].addr,
                                &s->m33_code_tcm);

    if (!memory_region_init_ram(&s->m33_data_tcm, OBJECT(dev),
                                fsl_imx95_memmap[FSL_IMX95_M33_DATA_TCM].name,
                                fsl_imx95_memmap[FSL_IMX95_M33_DATA_TCM].size,
                                errp)) {
        return false;
    }
    memory_region_add_subregion(sysmem,
                                fsl_imx95_memmap[FSL_IMX95_M33_DATA_TCM].addr,
                                &s->m33_data_tcm);

    if (!memory_region_init_ram(&s->m7_itcm, OBJECT(dev), "imx95.m7.itcm",
                                FSL_IMX95_M7_ITCM_SIZE, errp) ||
        !memory_region_init_ram(&s->m7_dtcm, OBJECT(dev), "imx95.m7.dtcm",
                                FSL_IMX95_M7_DTCM_SIZE, errp)) {
        return false;
    }
    memory_region_add_subregion(sysmem, FSL_IMX95_M7_TCM_SYS_ADDR,
                                &s->m7_itcm);
    memory_region_add_subregion(sysmem,
                                FSL_IMX95_M7_TCM_SYS_ADDR + FSL_IMX95_M7_ITCM_SIZE,
                                &s->m7_dtcm);
    memory_region_init_alias(&s->m7_itcm_sec_alias, OBJECT(dev),
                             "imx95.m7.itcm-secure", &s->m7_itcm, 0,
                             FSL_IMX95_M7_ITCM_SIZE);
    memory_region_add_subregion(sysmem, FSL_IMX95_M7_TCM_SEC_ADDR,
                                &s->m7_itcm_sec_alias);
    memory_region_init_alias(&s->m7_dtcm_sec_alias, OBJECT(dev),
                             "imx95.m7.dtcm-secure", &s->m7_dtcm, 0,
                             FSL_IMX95_M7_DTCM_SIZE);
    memory_region_add_subregion(sysmem,
                                FSL_IMX95_M7_TCM_SEC_ADDR + FSL_IMX95_M7_ITCM_SIZE,
                                &s->m7_dtcm_sec_alias);

    /* Holds the A55 SPL image placed there by the boot ROM. */
    if (!memory_region_init_ram(&s->a55_ocram, OBJECT(dev),
                                fsl_imx95_memmap[FSL_IMX95_A55_OCRAM].name,
                                fsl_imx95_memmap[FSL_IMX95_A55_OCRAM].size,
                                errp)) {
        return false;
    }
    memory_region_add_subregion(sysmem,
                                fsl_imx95_memmap[FSL_IMX95_A55_OCRAM].addr,
                                &s->a55_ocram);

    if (!memory_region_init_ram(&s->ddrmix, OBJECT(dev),
                                fsl_imx95_memmap[FSL_IMX95_DDRMIX].name,
                                fsl_imx95_memmap[FSL_IMX95_DDRMIX].size,
                                errp)) {
        return false;
    }
    memory_region_add_subregion(sysmem,
                                fsl_imx95_memmap[FSL_IMX95_DDRMIX].addr,
                                &s->ddrmix);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->sc_mu), errp)) {
        return false;
    }
    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(&s->sc_mu), 0,
                            fsl_imx95_memmap[FSL_IMX95_SC_MU].addr, 1);

    /* Cortex-A55 cores */
    qdev_prop_set_uint32(DEVICE(&s->a55_cluster), "cluster-id", 0);
    for (i = 0; i < FSL_IMX95_NUM_CPUS; i++) {
        uint64_t affinity = (uint64_t)i << 8;

        /* MT set: Aff1 is the core, Aff0 the lowest level */
        s->cpu[i].mpidr_mt = true;
        object_property_set_uint(OBJECT(&s->cpu[i]), "mp-affinity", affinity,
                                 errp);
        if (*errp) {
            return false;
        }
        object_property_set_uint(OBJECT(&s->cpu[i]), "cntfrq", 24000000,
                                 errp);
        if (*errp) {
            return false;
        }
        object_property_set_bool(OBJECT(&s->cpu[i]), "start-powered-off",
                                 true, errp);
        if (*errp) {
            return false;
        }

        if (!qdev_realize(DEVICE(&s->cpu[i]), NULL, errp)) {
            return false;
        }
        qemu_register_resettable(OBJECT(&s->cpu[i]));
    }

    if (!qdev_realize(DEVICE(&s->a55_cluster), NULL, errp)) {
        return false;
    }

    /* GIC */
    {
        SysBusDevice *gicsbd = SYS_BUS_DEVICE(&s->gic);
        QList *redist_region_count;

        qdev_prop_set_uint32(gicdev, "revision", 3);
        qdev_prop_set_uint32(gicdev, "num-cpu", FSL_IMX95_NUM_CPUS);
        qdev_prop_set_uint32(gicdev, "num-irq",
                             FSL_IMX95_NUM_IRQS + GIC_INTERNAL);
        qdev_prop_set_bit(gicdev, "has-security-extensions", true);
        qdev_prop_set_bit(gicdev, "has-lpi", true);
        redist_region_count = qlist_new();
        qlist_append_int(redist_region_count, FSL_IMX95_NUM_CPUS);
        qdev_prop_set_array(gicdev, "redist-region-count",
                            redist_region_count);
        object_property_set_link(OBJECT(&s->gic), "sysmem", OBJECT(sysmem),
                                 errp);
        if (*errp) {
            return false;
        }
        if (!sysbus_realize(gicsbd, errp)) {
            return false;
        }
        sysbus_mmio_map(gicsbd, 0, fsl_imx95_memmap[FSL_IMX95_GIC_DIST].addr);
        sysbus_mmio_map(gicsbd, 1,
                        fsl_imx95_memmap[FSL_IMX95_GIC_REDIST].addr);

        object_property_set_link(OBJECT(&s->its), "parent-gicv3",
                                 OBJECT(&s->gic), errp);
        if (*errp) {
            return false;
        }
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->its), errp)) {
            return false;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->its), 0,
                        fsl_imx95_memmap[FSL_IMX95_GIC_ITS].addr);

        /*
         * Wire each CPU's generic timer outputs and the GIC maintenance
         * interrupt to the GIC PPI inputs, and the GIC outputs to the CPU.
         */
        for (i = 0; i < FSL_IMX95_NUM_CPUS; i++) {
            DeviceState *cpudev = DEVICE(&s->cpu[i]);
            int intidbase = FSL_IMX95_NUM_IRQS + i * GIC_INTERNAL;

            qdev_connect_gpio_out(cpudev, GTIMER_PHYS,
                qdev_get_gpio_in(gicdev, intidbase + ARCH_TIMER_NS_EL1_IRQ));
            qdev_connect_gpio_out(cpudev, GTIMER_VIRT,
                qdev_get_gpio_in(gicdev, intidbase + ARCH_TIMER_VIRT_IRQ));
            qdev_connect_gpio_out(cpudev, GTIMER_HYP,
                qdev_get_gpio_in(gicdev, intidbase + ARCH_TIMER_NS_EL2_IRQ));
            qdev_connect_gpio_out(cpudev, GTIMER_SEC,
                qdev_get_gpio_in(gicdev, intidbase + ARCH_TIMER_S_EL1_IRQ));
            qdev_connect_gpio_out_named(cpudev, "gicv3-maintenance-interrupt",
                0, qdev_get_gpio_in(gicdev, intidbase + ARCH_GIC_MAINT_IRQ));

            sysbus_connect_irq(gicsbd, i,
                qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
            sysbus_connect_irq(gicsbd, i + FSL_IMX95_NUM_CPUS,
                qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
            sysbus_connect_irq(gicsbd, i + 2 * FSL_IMX95_NUM_CPUS,
                qdev_get_gpio_in(cpudev, ARM_CPU_VIRQ));
            sysbus_connect_irq(gicsbd, i + 3 * FSL_IMX95_NUM_CPUS,
                qdev_get_gpio_in(cpudev, ARM_CPU_VFIQ));
            sysbus_connect_irq(gicsbd, i + 6 * FSL_IMX95_NUM_CPUS,
                qdev_get_gpio_in_named(DEVICE(&s->blk_ctrl_aonmix),
                                       "gic-wakeup-req", i));
            qdev_connect_gpio_out_named(cpudev, "wfi-powerdown", 0,
                qdev_get_gpio_in_named(DEVICE(&s->blk_ctrl_aonmix),
                                       "a55-wfi-powerdown", i));
        }
    }

    clock_set_hz(s->m33_cpuclk, 24 * 1000 * 1000);
    clock_set_hz(s->m33_refclk, 24 * 1000 * 1000);
    qdev_connect_clock_in(m33dev, "cpuclk", s->m33_cpuclk);
    qdev_connect_clock_in(m33dev, "refclk", s->m33_refclk);
    object_property_set_link(OBJECT(&s->m33), "memory", OBJECT(sysmem),
                             errp);
    if (*errp) {
        return false;
    }
    qdev_prop_set_string(m33dev, "cpu-type", ARM_CPU_TYPE_NAME("cortex-m33"));
    qdev_prop_set_uint32(m33dev, "num-irq", FSL_IMX95_M33_NUM_IRQS);
    qdev_prop_set_uint32(m33dev, "init-svtor",
                         fsl_imx95_memmap[FSL_IMX95_M33_CODE_TCM].addr);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->m33), errp)) {
        return false;
    }
    g_assert(s->m33.cpu != NULL);
    CPU(s->m33.cpu)->cluster_index = 1;
    armv7m_load_kernel(s->m33.cpu, NULL, 0, 0);

    /* M7 view: system map, with ITCM and DTCM overlaid on top */
    memory_region_init(&s->m7_view, OBJECT(dev), "imx95.m7-view", 4 * GiB);
    memory_region_init_alias(&s->m7_sys_alias, OBJECT(dev), "imx95.m7-sys",
                             sysmem, 0, 4 * GiB);
    memory_region_add_subregion_overlap(&s->m7_view, 0, &s->m7_sys_alias, 0);
    memory_region_init_alias(&s->m7_itcm_alias, OBJECT(dev), "imx95.m7-itcm",
                             &s->m7_itcm, 0, FSL_IMX95_M7_ITCM_SIZE);
    memory_region_add_subregion_overlap(&s->m7_view, 0, &s->m7_itcm_alias, 1);
    memory_region_init_alias(&s->m7_dtcm_alias, OBJECT(dev), "imx95.m7-dtcm",
                             &s->m7_dtcm, 0, FSL_IMX95_M7_DTCM_SIZE);
    memory_region_add_subregion_overlap(&s->m7_view, FSL_IMX95_M7_DTCM_BASE,
                                        &s->m7_dtcm_alias, 1);

    clock_set_hz(s->m7_cpuclk, 24 * 1000 * 1000);
    clock_set_hz(s->m7_refclk, 24 * 1000 * 1000);
    qdev_connect_clock_in(DEVICE(&s->m7), "cpuclk", s->m7_cpuclk);
    qdev_connect_clock_in(DEVICE(&s->m7), "refclk", s->m7_refclk);
    object_property_set_link(OBJECT(&s->m7), "memory", OBJECT(&s->m7_view),
                             errp);
    if (*errp) {
        return false;
    }
    qdev_prop_set_string(DEVICE(&s->m7), "cpu-type",
                         ARM_CPU_TYPE_NAME("cortex-m7"));
    qdev_prop_set_uint32(DEVICE(&s->m7), "num-irq", FSL_IMX95_M7_NUM_IRQS);
    qdev_prop_set_bit(DEVICE(&s->m7), "start-powered-off", true);
    qdev_prop_set_uint32(DEVICE(&s->m7), "mpu-ns-regions", 16);
    qdev_prop_set_uint32(DEVICE(&s->m7), "mpu-s-regions", 16);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->m7), errp)) {
        return false;
    }
    g_assert(s->m7.cpu != NULL);
    CPU(s->m7.cpu)->cluster_index = 2;
    armv7m_load_kernel(s->m7.cpu, NULL, 0, 0);
    qdev_init_gpio_in_named(dev, fsl_imx95_m7_run_in, "m7-run", 1);
    qdev_connect_gpio_out_named(DEVICE(&s->blk_ctrl_aonmix), "m7-run", 0,
                                qdev_get_gpio_in_named(dev, "m7-run", 0));

    {
        Chardev *chr = serial_hd(2);
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->m7_lpuart);

        if (chr) {
            qdev_prop_set_chr(DEVICE(&s->m7_lpuart), "chardev", chr);
        }
        if (!sysbus_realize(sbd, errp)) {
            return false;
        }
        sysbus_mmio_map(sbd, 0, fsl_imx95_memmap[FSL_IMX95_LPUART_M7].addr);
        sysbus_connect_irq(sbd, 0, qdev_get_gpio_in(DEVICE(&s->m7),
                                                    FSL_IMX95_LPUART_M7_IRQ));
    }

    return true;
}

static bool fsl_imx95_realize_communication(FslImx95State *s,
                                             DeviceState *dev,
                                             MemoryRegion *sysmem,
                                             DeviceState *gicdev,
                                             DeviceState *m33dev,
                                             Error **errp)
{
    int i;

    /* AON message units (A side -> GIC, B side -> M33 NVIC) */
    for (i = 0; i < ARRAY_SIZE(s->aon_mu); i++) {
        SysBusDevice *mu = SYS_BUS_DEVICE(&s->aon_mu[i]);
        qdev_prop_set_uint32(DEVICE(&s->aon_mu[i]),
                              "gier0-reset", BIT(0));
        qdev_prop_set_uint32(DEVICE(&s->aon_mu[i]),
                              "gier1-reset", BIT(0));

        if (!memory_region_init_ram(&s->aon_mu_sram[i], OBJECT(dev),
                                    fsl_imx95_memmap[fsl_imx95_aon_mu_cfg[i].sram_a_region].name,
                                    fsl_imx95_memmap[fsl_imx95_aon_mu_cfg[i].sram_a_region].size,
                                    errp)) {
            return false;
        }
        memory_region_add_subregion(sysmem,
                                    fsl_imx95_memmap[fsl_imx95_aon_mu_cfg[i].sram_a_region].addr,
                                    &s->aon_mu_sram[i]);
        memory_region_init_alias(&s->aon_mu_sram_b_alias[i], OBJECT(dev),
                                 fsl_imx95_memmap[fsl_imx95_aon_mu_cfg[i].sram_b_region].name,
                                 &s->aon_mu_sram[i], 0,
                                 fsl_imx95_memmap[fsl_imx95_aon_mu_cfg[i].sram_b_region].size);
        memory_region_add_subregion(sysmem,
                                    fsl_imx95_memmap[fsl_imx95_aon_mu_cfg[i].sram_b_region].addr,
                                    &s->aon_mu_sram_b_alias[i]);

        if (!sysbus_realize(mu, errp)) {
            return false;
        }
        sysbus_mmio_map(mu, 0, fsl_imx95_memmap[fsl_imx95_aon_mu_cfg[i].a_region].addr);
        sysbus_mmio_map(mu, 1, fsl_imx95_memmap[fsl_imx95_aon_mu_cfg[i].b_region].addr);
        sysbus_connect_irq(mu, 0, qdev_get_gpio_in(gicdev, fsl_imx95_aon_mu_cfg[i].mua_irq));
        sysbus_connect_irq(mu, 1, qdev_get_gpio_in(m33dev, fsl_imx95_aon_mu_cfg[i].mub_irq));
    }

    if (!memory_region_init_ram(&s->m7_mu_sram, OBJECT(dev),
                                fsl_imx95_memmap[FSL_IMX95_AON_MU5_SRAM_A].name,
                                fsl_imx95_memmap[FSL_IMX95_AON_MU5_SRAM_A].size,
                                errp)) {
        return false;
    }
    memory_region_add_subregion(sysmem,
                                fsl_imx95_memmap[FSL_IMX95_AON_MU5_SRAM_A].addr,
                                &s->m7_mu_sram);
    memory_region_init_alias(&s->m7_mu_sram_b_alias, OBJECT(dev),
                             fsl_imx95_memmap[FSL_IMX95_AON_MU5_SRAM_B].name,
                             &s->m7_mu_sram, 0,
                             fsl_imx95_memmap[FSL_IMX95_AON_MU5_SRAM_B].size);
    memory_region_add_subregion(sysmem,
                                fsl_imx95_memmap[FSL_IMX95_AON_MU5_SRAM_B].addr,
                                &s->m7_mu_sram_b_alias);
    qdev_prop_set_uint32(DEVICE(&s->m7_mu), "gier0-reset", BIT(0));
    qdev_prop_set_uint32(DEVICE(&s->m7_mu), "gier1-reset", BIT(0));
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->m7_mu), errp)) {
        return false;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->m7_mu), 0,
                    fsl_imx95_memmap[FSL_IMX95_AON_MU5_A].addr);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->m7_mu), 1,
                    fsl_imx95_memmap[FSL_IMX95_AON_MU5_B].addr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->m7_mu), 0,
                       qdev_get_gpio_in(DEVICE(&s->m7),
                                        FSL_IMX95_AON_MU5_MUA_M7_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->m7_mu), 1,
                       qdev_get_gpio_in(m33dev,
                                        FSL_IMX95_AON_MU5_MUB_M33_IRQ));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->wakeup_mu), errp)) {
        return false;
    }

    sysbus_mmio_map(SYS_BUS_DEVICE(&s->wakeup_mu), 0,
                    fsl_imx95_memmap[FSL_IMX95_WAKEUP_MU].addr);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->wakeup_mu), 1,
                    fsl_imx95_memmap[FSL_IMX95_WAKEUP_MU_B].addr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->wakeup_mu), 0,
                       qdev_get_gpio_in(gicdev, FSL_IMX95_WAKEUP_MU7_MUA_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->wakeup_mu), 1,
                       qdev_get_gpio_in(DEVICE(&s->m7),
                                        FSL_IMX95_WAKEUP_MU7_MUB_M7_IRQ));

    for (i = 0; i < FSL_IMX95_NUM_LPUARTS; i++) {
        Chardev *chr = serial_hd(fsl_imx95_lpuart_cfg[i].serial);
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->lpuart[i]);

        if (chr) {
            qdev_prop_set_chr(DEVICE(&s->lpuart[i]), "chardev", chr);
        }
        if (!sysbus_realize(sbd, errp)) {
            return false;
        }
        sysbus_mmio_map(sbd, 0, fsl_imx95_memmap[fsl_imx95_lpuart_cfg[i].region].addr);
        sysbus_connect_irq(sbd, 0,
            qdev_get_gpio_in(fsl_imx95_lpuart_cfg[i].to_gic ? gicdev : m33dev,
                             fsl_imx95_lpuart_cfg[i].irq));
    }

    {
        MemoryRegion *a55_uart_mr = sysbus_mmio_get_region(
            SYS_BUS_DEVICE(&s->lpuart[FSL_IMX95_NUM_LPUARTS - 1]), 0);

        memory_region_init_alias(&s->lpuart_a55_alias, OBJECT(dev),
            fsl_imx95_memmap[FSL_IMX95_LPUART_A55_ALIAS].name, a55_uart_mr, 0,
            memory_region_size(a55_uart_mr));
        memory_region_add_subregion(sysmem,
            fsl_imx95_memmap[FSL_IMX95_LPUART_A55_ALIAS].addr,
            &s->lpuart_a55_alias);
    }

    for (i = 0; i < ARRAY_SIZE(s->edma); i++) {
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->edma[i]);
        DeviceState *edev = DEVICE(&s->edma[i]);
        bool v5 = fsl_imx95_edma_cfg[i].version == IMX_EDMA_V5;

        unsigned int ch;
        unsigned int nlines = v5 ?
            fsl_imx95_edma_cfg[i].channels / 2 :
            fsl_imx95_edma_cfg[i].channels;

        /* 1 IRQ output for every channel or channel pair. */

        qdev_prop_set_uint32(edev, "version", fsl_imx95_edma_cfg[i].version);
        qdev_prop_set_uint32(edev, "num-channels", fsl_imx95_edma_cfg[i].channels);
        qdev_prop_set_uint32(edev, "num-requests", fsl_imx95_edma_cfg[i].requests);
        qdev_prop_set_bit(edev, "paired-irqs", v5);
        object_property_set_link(OBJECT(edev), "dma", OBJECT(sysmem),
                                 errp);
        if (*errp) {
            return false;
        }
        if (!sysbus_realize(sbd, errp)) {
            return false;
        }
        sysbus_mmio_map(sbd, 0, fsl_imx95_memmap[fsl_imx95_edma_cfg[i].region].addr);
        for (ch = 0; ch < fsl_imx95_edma_cfg[i].irq_lines; ch++) {
            sysbus_connect_irq(sbd, ch,
                qdev_get_gpio_in(gicdev, fsl_imx95_edma_cfg[i].irq_base + ch));
        }
        sysbus_connect_irq(sbd, nlines,
                           qdev_get_gpio_in(gicdev, fsl_imx95_edma_cfg[i].err_irq));
    }

    for (i = 0; i < FSL_IMX95_NUM_LPI2CS; i++) {
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->lpi2c[i]);

        if (!sysbus_realize(sbd, errp)) {
            return false;
        }
        sysbus_mmio_map(sbd, 0, fsl_imx95_memmap[fsl_imx95_lpi2c_cfg[i].region].addr);
        sysbus_connect_irq(sbd, 0,
            qdev_get_gpio_in(i == 0 ? m33dev : gicdev, fsl_imx95_lpi2c_cfg[i].irq));
        qdev_connect_gpio_out_named(DEVICE(sbd), "tx-dma-req", 0,
            qdev_get_gpio_in_named(DEVICE(&s->edma[fsl_imx95_lpi2c_cfg[i].edma]),
                                   "dma-req", fsl_imx95_lpi2c_cfg[i].tx_request));
        qdev_connect_gpio_out_named(DEVICE(sbd), "rx-dma-req", 0,
            qdev_get_gpio_in_named(DEVICE(&s->edma[fsl_imx95_lpi2c_cfg[i].edma]),
                                   "dma-req", fsl_imx95_lpi2c_cfg[i].rx_request));
    }

    return true;
}

static bool fsl_imx95_realize_peripherals(FslImx95State *s,
                                           DeviceState *dev,
                                           MemoryRegion *sysmem,
                                           DeviceState *gicdev,
                                           Error **errp)
{
    int i;

    /* uSDHCs */
    for (i = 0; i < FSL_IMX95_NUM_USDHCS; i++) {

        if (!sysbus_realize(SYS_BUS_DEVICE(&s->usdhc[i]), errp)) {
            return false;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->usdhc[i]), 0,
                        fsl_imx95_memmap[fsl_imx95_usdhc_cfg[i].region].addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->usdhc[i]), 0,
                           qdev_get_gpio_in(gicdev, fsl_imx95_usdhc_cfg[i].irq));
    }

    qdev_prop_set_uint32(DEVICE(&s->usb1), "slots", 8);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->usb1), errp)) {
        return false;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->usb1), 0,
                    fsl_imx95_memmap[FSL_IMX95_USB1].addr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->usb1.sysbus_xhci), 0,
                       qdev_get_gpio_in(gicdev, FSL_IMX95_USB1_IRQ));
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->usb2), errp)) {
        return false;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->usb2), 0,
                    fsl_imx95_memmap[FSL_IMX95_USB2].addr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->usb2), 0,
                       qdev_get_gpio_in(gicdev, FSL_IMX95_USB2_IRQ));

    {
        unsigned n;

        for (n = 0; n < ARRAY_SIZE(s->pcie); n++) {
            SysBusDevice *sbd = SYS_BUS_DEVICE(&s->pcie[n]);
            g_autofree char *bus = g_strdup_printf("imx95-pcie%u", n);

            qdev_prop_set_string(DEVICE(sbd), "bus-name", bus);
            qdev_prop_set_uint32(DEVICE(sbd), "domain-nr", n);
            if (!sysbus_realize(sbd, errp)) {
                return false;
            }
            sysbus_mmio_map(sbd, 0, fsl_imx95_memmap[fsl_imx95_pcie_cfg[n].dbi_region].addr);
            sysbus_mmio_map(sbd, 1, fsl_imx95_memmap[fsl_imx95_pcie_cfg[n].atu_region].addr);
            for (i = 0; i < 4; i++) {
                sysbus_connect_irq(sbd, i,
                    qdev_get_gpio_in(gicdev, fsl_imx95_pcie_cfg[n].inta_irq + i));
            }
            /* LTSSM_EN of the SerDes_SS starts training of this controller */
            qdev_connect_gpio_out_named(
                DEVICE(&s->pcie_serdes[n]), "ltssm-en", 0,
                qdev_get_gpio_in_named(DEVICE(sbd), "ltssm-en", 0));
            sysbus_connect_irq(sbd, 4,
                qdev_get_gpio_in(gicdev, fsl_imx95_pcie_cfg[n].msi_irq));
        }
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->tpm[0]), errp)) {
        return false;
    }
    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(&s->tpm[0]), 0,
                            fsl_imx95_memmap[FSL_IMX95_TPM3].addr, 1);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->tpm[1]), errp)) {
        return false;
    }
    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(&s->tpm[1]), 0,
                            fsl_imx95_memmap[FSL_IMX95_TPM6].addr, 1);
    for (i = 0; i < ARRAY_SIZE(s->wdog); i++) {
        if (!fsl_imx95_realize_simple_device(SYS_BUS_DEVICE(&s->wdog[i]),
                                             &fsl_imx95_wdog_cfg[i],
                                             gicdev, errp)) {
            return false;
        }
    }

    for (i = 0; i < ARRAY_SIZE(s->gpio); i++) {
        if (!fsl_imx95_realize_simple_device(SYS_BUS_DEVICE(&s->gpio[i]),
                                             &fsl_imx95_gpio_cfg[i],
                                             gicdev, errp)) {
            return false;
        }
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->adc), errp)) {
        return false;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->adc), 0, fsl_imx95_memmap[FSL_IMX95_ADC].addr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->adc), 0,
                       qdev_get_gpio_in(gicdev, FSL_IMX95_ADC_IRQ));
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->src_gen), errp)) {
        return false;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->src_gen), 0, fsl_imx95_memmap[FSL_IMX95_SRC_GEN].addr);

    for (i = 0; i < FSL_IMX95_NUM_CPUS + 1; i++) {
        qdev_connect_gpio_out_named(DEVICE(&s->src_gen), "a55-hold", i,
            qdev_get_gpio_in_named(DEVICE(&s->blk_ctrl_aonmix), "a55-hold", i));
    }
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->fspi), errp)) {
        return false;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->fspi), 0, fsl_imx95_memmap[FSL_IMX95_FSPI1_REG].addr);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->smmuv3), errp)) {
        return false;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->smmuv3), 0, fsl_imx95_memmap[FSL_IMX95_SMMUV3].addr);

    for (i = 0; i < IMX95_SMMU_PMCG_NUM; i++) {
        hwaddr base = i == 0 ? FSL_IMX95_SMMU_PMCG0_BASE :
                      FSL_IMX95_SMMU_PMCG1_BASE +
                      (i - 1) * FSL_IMX95_SMMU_PMCG_STRIDE;

        if (!sysbus_realize(SYS_BUS_DEVICE(&s->smmu_pmcg[i]), errp)) {
            return false;
        }
        sysbus_mmio_map_overlap(SYS_BUS_DEVICE(&s->smmu_pmcg[i]), 0, base, 1);
    }
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->tmpsns), errp)) {
        return false;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->tmpsns), 0, fsl_imx95_memmap[FSL_IMX95_A55_TMPSNS].addr);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->fractpll), errp)) {
        return false;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->fractpll), 0, fsl_imx95_memmap[FSL_IMX95_FRACTPLL].addr);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->bbnsm), errp)) {
        return false;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->bbnsm), 0, fsl_imx95_memmap[FSL_IMX95_BBNSM].addr);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->ccm), errp)) {
        return false;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ccm), 0,
                    fsl_imx95_memmap[FSL_IMX95_CCM].addr);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->ddrc), errp)) {
        return false;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ddrc), 0,
                    fsl_imx95_memmap[FSL_IMX95_DDRC].addr);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->sysctr), errp)) {
        return false;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sysctr), 0,
                    fsl_imx95_memmap[FSL_IMX95_SYSCNT_CTRL].addr);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sysctr), 1,
                    fsl_imx95_memmap[FSL_IMX95_SYSCNT_CMP].addr);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sysctr), 2,
                    fsl_imx95_memmap[FSL_IMX95_SYSCNT_READ].addr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->sysctr), 0,
                       qdev_get_gpio_in(gicdev, FSL_IMX95_SYSCTR_IRQ));

    for (i = 0; i < 2; i++) {

        qdev_prop_set_uint32(DEVICE(&s->pcie_serdes[i]), "devid-prefix",
                             i == 0 ? 0x00 : 0x80);
        qdev_prop_set_uint64(DEVICE(&s->pcie_serdes[i]), "msi-target",
                             fsl_imx95_memmap[FSL_IMX95_GIC_ITS].addr +
                             ITS_CONTROL_SIZE + GITS_TRANSLATER);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->pcie_serdes[i]), errp)) {
            return false;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->pcie_serdes[i]), 0,
                        fsl_imx95_memmap[fsl_imx95_serdes_region[i]].addr);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->netc_ecam[i]), errp)) {
            return false;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->netc_ecam[i]), 0,
                        fsl_imx95_memmap[fsl_imx95_netc_ecam_region[i]].addr);
    }

    /* MSI writes routed to the GIC ITS translator aperture. */
    for (i = 0; i < ARRAY_SIZE(s->pcie); i++) {
        imx95_pcie_host_add_dma_window(
            &s->pcie[i],
            fsl_imx95_memmap[FSL_IMX95_GIC_ITS].addr + ITS_CONTROL_SIZE +
            GITS_TRANSLATER,
            sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->pcie_serdes[i]), 1),
            10);
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->hsio_blk_ctrl), errp)) {
        return false;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->hsio_blk_ctrl), 0,
                    fsl_imx95_memmap[FSL_IMX95_HSIO_BLK_CTRL].addr);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->camera_csr), errp)) {
        return false;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->camera_csr), 0,
                    fsl_imx95_memmap[FSL_IMX95_CAMERA_CSR].addr);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->camera_noc), errp)) {
        return false;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->camera_noc), 0,
                    fsl_imx95_memmap[FSL_IMX95_CAMERA_NOC].addr);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->blk_ctrl_aonmix), errp)) {
        return false;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->blk_ctrl_aonmix), 0,
                    fsl_imx95_memmap[FSL_IMX95_BLK_CTRL_S_AONMIX].addr);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->ele_mu), errp)) {
        return false;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ele_mu), 0,
                    fsl_imx95_memmap[FSL_IMX95_ELE_MU].addr);
    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(&s->ele_mu), 1,
                            fsl_imx95_memmap[FSL_IMX95_FSB].addr, 1);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->ele_mu), 0,
                       qdev_get_gpio_in(gicdev, FSL_IMX95_ELE_MU3_IRQ));
    qdev_connect_gpio_out_named(DEVICE(&s->blk_ctrl_aonmix), "ele-mu3-irq-mask",
                                0, qdev_get_gpio_in_named(DEVICE(&s->ele_mu),
                                                          "ca55-irq-mask", 0));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->v2x_dbg_mu), errp)) {
        return false;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->v2x_dbg_mu), 0,
                    fsl_imx95_memmap[FSL_IMX95_V2X_DBG_MU].addr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->v2x_dbg_mu), 0,
                       qdev_get_gpio_in(gicdev, FSL_IMX95_V2X_DBG_MU_IRQ));


    return true;
}

static void fsl_imx95_create_unimplemented_devices(void)
{
    int i;

    /* Unimplemented devices */
    for (i = 0; i < ARRAY_SIZE(fsl_imx95_memmap); i++) {
        switch (i) {
        case FSL_IMX95_A55_OCRAM:
        case FSL_IMX95_A55_TMPSNS:
        case FSL_IMX95_ADC:
        case FSL_IMX95_AON_MU1_A:
        case FSL_IMX95_AON_MU1_B:
        case FSL_IMX95_AON_MU1_SRAM_A:
        case FSL_IMX95_AON_MU1_SRAM_B:
        case FSL_IMX95_AON_MU2_A:
        case FSL_IMX95_AON_MU2_B:
        case FSL_IMX95_AON_MU2_SRAM_A:
        case FSL_IMX95_AON_MU2_SRAM_B:
        case FSL_IMX95_AON_MU5_A:
        case FSL_IMX95_AON_MU5_B:
        case FSL_IMX95_AON_MU5_SRAM_A:
        case FSL_IMX95_AON_MU5_SRAM_B:
        case FSL_IMX95_LPUART_M7:
        case FSL_IMX95_BBNSM:
        case FSL_IMX95_BLK_CTRL_S_AONMIX:
        case FSL_IMX95_CAMERA_CSR:
        case FSL_IMX95_CAMERA_NOC:
        case FSL_IMX95_CCM:
        case FSL_IMX95_DDRC:
        case FSL_IMX95_HSIO_BLK_CTRL:
        case FSL_IMX95_DDRMIX:
        case FSL_IMX95_ELE_MU:
        case FSL_IMX95_FSB:
        case FSL_IMX95_FRACTPLL:
        case FSL_IMX95_FSPI1_REG:
        case FSL_IMX95_GIC_DIST:
        case FSL_IMX95_GIC_ITS:
        case FSL_IMX95_GIC_REDIST:
        case FSL_IMX95_GPIO1 ... FSL_IMX95_GPIO5:
        case FSL_IMX95_EDMA1 ... FSL_IMX95_EDMA3:
        case FSL_IMX95_LPI2C1 ... FSL_IMX95_LPI2C8:
        case FSL_IMX95_LPUART_A55:
        case FSL_IMX95_LPUART_A55_ALIAS:
        case FSL_IMX95_SYSCNT_CTRL:
        case FSL_IMX95_SYSCNT_CMP:
        case FSL_IMX95_SYSCNT_READ:
        case FSL_IMX95_LPUART_SM:
        case FSL_IMX95_M33_CODE_TCM:
        case FSL_IMX95_M33_DATA_TCM:
        case FSL_IMX95_NETC_ECAM0:
        case FSL_IMX95_NETC_ECAM1:
        case FSL_IMX95_PCIE0:
        case FSL_IMX95_PCIE0_APP:
        case FSL_IMX95_PCIE0_ATU:
        case FSL_IMX95_PCIE1:
        case FSL_IMX95_PCIE1_APP:
        case FSL_IMX95_PCIE1_ATU:
        case FSL_IMX95_RAM:
        case FSL_IMX95_SC_MU:
        case FSL_IMX95_SMMUV3:
        case FSL_IMX95_SRC_GEN:
        case FSL_IMX95_TPM3:
        case FSL_IMX95_TPM6:
        case FSL_IMX95_USB1:
        case FSL_IMX95_USB2:
        case FSL_IMX95_USDHC1 ... FSL_IMX95_USDHC3:
        case FSL_IMX95_V2X_DBG_MU:
        case FSL_IMX95_WAKEUP_MU:
        case FSL_IMX95_WAKEUP_MU_B:
        case FSL_IMX95_WDOG1:
        case FSL_IMX95_WDOG2:
        case FSL_IMX95_WDOG4:
        case FSL_IMX95_WDOG5:
            break;

        default:
            create_unimplemented_device(fsl_imx95_memmap[i].name,
                                        fsl_imx95_memmap[i].addr,
                                        fsl_imx95_memmap[i].size);
            break;
        }
    }}

static void fsl_imx95_realize(DeviceState *dev, Error **errp)
{
    FslImx95State *s = FSL_IMX95(dev);
    MemoryRegion *sysmem = get_system_memory();
    DeviceState *gicdev = DEVICE(&s->gic);
    DeviceState *m33dev = DEVICE(&s->m33);

    if (!fsl_imx95_realize_core(s, dev, sysmem, gicdev, m33dev, errp) ||
        !fsl_imx95_realize_communication(s, dev, sysmem, gicdev, m33dev,
                                         errp) ||
        !fsl_imx95_realize_peripherals(s, dev, sysmem, gicdev, errp)) {
        return;
    }

    fsl_imx95_create_unimplemented_devices();
}

I2CBus *fsl_imx95_get_i2c_bus(FslImx95State *s, unsigned int index)
{
    g_assert(index < FSL_IMX95_NUM_LPI2CS);
    g_assert(s->lpi2c[index].bus != NULL);

    return s->lpi2c[index].bus;
}

BusState *fsl_imx95_get_usdhc_bus(FslImx95State *s, unsigned int index)
{
    BusState *bus;

    g_assert(index < FSL_IMX95_NUM_USDHCS);

    bus = qdev_get_child_bus(DEVICE(&s->usdhc[index]), "sd-bus");
    g_assert(bus != NULL);

    return bus;
}

static void fsl_imx95_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = fsl_imx95_realize;
    dc->desc = "i.MX95 SoC";
}

static const TypeInfo fsl_imx95_types[] = {
    {
        .name = TYPE_FSL_IMX95,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(FslImx95State),
        .instance_init = fsl_imx95_init,
        .class_init = fsl_imx95_class_init,
    },
};

DEFINE_TYPES(fsl_imx95_types)


