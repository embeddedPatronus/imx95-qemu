/*
 * NXP i.MX95 SoC emulation
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef FSL_IMX95_H
#define FSL_IMX95_H

#include "hw/core/sysbus.h"
#include "hw/arm/armv7m.h"
#include "hw/cpu/cluster.h"
#include "hw/char/imx_lpuart.h"
#include "hw/dma/imx_edma.h"
#include "hw/i2c/imx_lpi2c.h"
#include "hw/intc/arm_gicv3_common.h"
#include "hw/intc/arm_gicv3_its_common.h"
#include "hw/misc/imx95_mu.h"
#include "hw/misc/imx95_v2x_dbg_mu.h"
#include "hw/misc/imx95_sc_mu.h"
#include "hw/misc/imx95_ele_mu.h"
#include "hw/misc/imx95_blk_ctrl_s_aonmix.h"
#include "hw/misc/imx95_tpm_pwm.h"
#include "hw/misc/imx95_wdog32.h"
#include "hw/misc/imx95_gpio.h"
#include "hw/misc/imx95_adc.h"
#include "hw/misc/imx95_src_gen.h"
#include "hw/misc/imx95_fspi.h"
#include "hw/misc/imx95_smmuv3.h"
#include "hw/misc/imx95_tmpsns.h"
#include "hw/misc/imx95_fractpll.h"
#include "hw/misc/imx95_bbnsm.h"
#include "hw/misc/imx95_camera_csr.h"
#include "hw/misc/imx95_camera_noc.h"
#include "hw/misc/imx95_ccm.h"
#include "hw/misc/imx95_ddrc.h"
#include "hw/timer/imx95_sysctr.h"
#include "hw/misc/imx95_netc_ecam.h"
#include "hw/misc/imx95_pcie_serdes_ss.h"
#include "hw/misc/imx95_hsio_blk_ctrl.h"
#include "hw/pci-host/imx95_pcie_host.h"
#include "hw/sd/sdhci.h"
#include "hw/usb/chipidea.h"
#include "hw/usb/hcd-dwc3.h"
#include "qom/object.h"
#include "qemu/units.h"
#include "target/arm/cpu.h"

#define TYPE_FSL_IMX95 "fsl-imx95"
OBJECT_DECLARE_SIMPLE_TYPE(FslImx95State, FSL_IMX95)

#define FSL_IMX95_RAM_START        0x80000000
#define FSL_IMX95_RAM_SIZE_MAX     (16 * GiB)

enum FslImx95Configuration {
    FSL_IMX95_NUM_CPUS         = 6,    /* Cortex-A55 */
    FSL_IMX95_NUM_LPI2CS       = 8,
    FSL_IMX95_NUM_LPUARTS      = 2,    /* SM debug, Cortex-A55 console */
    FSL_IMX95_NUM_USDHCS       = 3,
    FSL_IMX95_NUM_EDMAS        = 3,
    FSL_IMX95_NUM_AON_MUS      = 2,
    FSL_IMX95_NUM_PCIES        = 2,
    FSL_IMX95_NUM_GPIOS        = 5,
    FSL_IMX95_NUM_WDOGS        = 4,
    FSL_IMX95_NUM_TPMS         = 2,
    FSL_IMX95_NUM_IRQS         = 384,
    FSL_IMX95_M33_NUM_IRQS     = 376,
};

struct FslImx95State {
    SysBusDevice    parent_obj;

    CPUClusterState a55_cluster;
    ARMCPU          cpu[FSL_IMX95_NUM_CPUS];
    GICv3State      gic;
    GICv3ITSState   its;
    ARMv7MState     m33;
    Clock          *m33_cpuclk;
    Clock          *m33_refclk;
    ARMv7MState     m7;
    Clock          *m7_cpuclk;
    Clock          *m7_refclk;

    IMX95MUState    aon_mu[FSL_IMX95_NUM_AON_MUS];
    IMX95MUState    wakeup_mu;
    IMX95MUState    m7_mu;
    MemoryRegion    m7_mu_sram;
    MemoryRegion    m7_mu_sram_b_alias;
    IMXLPUARTState  lpuart[FSL_IMX95_NUM_LPUARTS];
    IMXLPUARTState  m7_lpuart;
    IMXEDMAState    edma[FSL_IMX95_NUM_EDMAS];
    IMXLPI2CState   lpi2c[FSL_IMX95_NUM_LPI2CS];
    SDHCIState      usdhc[FSL_IMX95_NUM_USDHCS];
    USBDWC3         usb1;
    ChipideaState   usb2;
    Imx95PcieHost   pcie[FSL_IMX95_NUM_PCIES];

    MemoryRegion    m33_code_tcm;
    MemoryRegion    m33_data_tcm;
    MemoryRegion    a55_ocram;
    MemoryRegion    ddrmix;
    MemoryRegion    aon_mu_sram[FSL_IMX95_NUM_AON_MUS];
    MemoryRegion    aon_mu_sram_b_alias[FSL_IMX95_NUM_AON_MUS];
    MemoryRegion    lpuart_a55_alias;
    MemoryRegion    m7_itcm;
    MemoryRegion    m7_dtcm;
    MemoryRegion    m7_itcm_sec_alias;
    MemoryRegion    m7_dtcm_sec_alias;
    MemoryRegion    m7_view;
    MemoryRegion    m7_sys_alias;
    MemoryRegion    m7_itcm_alias;
    MemoryRegion    m7_dtcm_alias;

    Imx95CcmState ccm;
    Imx95DdrcState ddrc;
    Imx95SysctrState sysctr;
    Imx95PcieSerdesSsState pcie_serdes[FSL_IMX95_NUM_PCIES];
    Imx95NetcEcamState netc_ecam[FSL_IMX95_NUM_PCIES];
    Imx95HsioBlkCtrlState hsio_blk_ctrl;
    Imx95CameraCsrState camera_csr;
    Imx95CameraNocState camera_noc;

    Imx95BbnsmState bbnsm;

    Imx95FractpllState fractpll;

    Imx95TmpsnsState tmpsns;

    Imx95Smmuv3State smmuv3;
    Imx95SmmuPmcgState smmu_pmcg[IMX95_SMMU_PMCG_NUM];

    Imx95FspiState fspi;

    Imx95SrcGenState src_gen;

    Imx95AdcState adc;

    Imx95GpioState gpio[FSL_IMX95_NUM_GPIOS];

    Imx95Wdog32State wdog[FSL_IMX95_NUM_WDOGS];

    Imx95TpmPwmState tpm[FSL_IMX95_NUM_TPMS];

    Imx95BlkCtrlSAonmixState blk_ctrl_aonmix;
    Imx95EleMuState ele_mu;
    Imx95V2xDbgMuState v2x_dbg_mu;
    Imx95ScMuState sc_mu;

};

I2CBus *fsl_imx95_get_i2c_bus(FslImx95State *s, unsigned int index);
BusState *fsl_imx95_get_usdhc_bus(FslImx95State *s, unsigned int index);

enum FslImx95Irqs {
    FSL_IMX95_FSPI1_IRQ             = 48,
    FSL_IMX95_LPUART_SM_IRQ         = 19,
    FSL_IMX95_LPUART_M7_IRQ         = 64,
    FSL_IMX95_SYSCTR_IRQ            = 72,
    FSL_IMX95_LPUART_A55_IRQ        = 19,
    FSL_IMX95_LPI2C1_IRQ            = 13,
    FSL_IMX95_LPI2C2_IRQ            = 14,
    FSL_IMX95_LPI2C3_IRQ            = 58,
    FSL_IMX95_LPI2C4_IRQ            = 59,
    FSL_IMX95_LPI2C5_IRQ            = 181,
    FSL_IMX95_LPI2C6_IRQ            = 182,
    FSL_IMX95_LPI2C7_IRQ            = 183,
    FSL_IMX95_LPI2C8_IRQ            = 184,
    FSL_IMX95_USDHC1_IRQ            = 86,
    FSL_IMX95_USDHC2_IRQ            = 87,
    FSL_IMX95_USDHC3_IRQ            = 191,
    FSL_IMX95_USB1_IRQ              = 175,
    FSL_IMX95_USB2_IRQ              = 176,
    FSL_IMX95_ADC_IRQ = 199,
    FSL_IMX95_ELE_MU3_IRQ           = 24,
    FSL_IMX95_V2X_DBG_MU_IRQ        = 255,
    FSL_IMX95_AON_MU1_MUA_IRQ       = 224,
    FSL_IMX95_AON_MU1_MUB_M33_IRQ   = 225,  /* NVIC */
    FSL_IMX95_AON_MU2_MUA_IRQ       = 226,
    FSL_IMX95_AON_MU2_MUB_M33_IRQ   = 227,  /* NVIC */
    FSL_IMX95_AON_MU5_MUA_M7_IRQ    = 205,
    FSL_IMX95_AON_MU5_MUB_M33_IRQ   = 232,
    FSL_IMX95_WAKEUP_MU7_MUA_IRQ    = 234,
    FSL_IMX95_WAKEUP_MU7_MUB_M7_IRQ = 207,
    FSL_IMX95_PCIE0_INTA_IRQ        = 306,
    FSL_IMX95_PCIE0_MSI_IRQ         = 310,
    FSL_IMX95_PCIE1_INTA_IRQ        = 312,
    FSL_IMX95_PCIE1_MSI_IRQ         = 316,
    FSL_IMX95_PCIE1_DMA_IRQ         = 317,
    FSL_IMX95_EDMA_AON_IRQ          = 96,
    FSL_IMX95_EDMA_AON_ERR_IRQ      = 95,
    FSL_IMX95_EDMA_WAKEUP2_IRQ      = 128,
    FSL_IMX95_EDMA_WAKEUP2_ERR_IRQ  = 127,
    FSL_IMX95_EDMA_WAKEUP3_IRQ      = 256,
    FSL_IMX95_EDMA_WAKEUP3_ERR_IRQ  = 250,
};

enum FslImx95Edmas {
    FSL_IMX95_EDMA_AON,
    FSL_IMX95_EDMA_WAKEUP2,
    FSL_IMX95_EDMA_WAKEUP3,
};

enum FslImx95DmaRequests {
    FSL_IMX95_DMAREQ_LPI2C1_TX = 12,
    FSL_IMX95_DMAREQ_LPI2C1_RX = 13,
    FSL_IMX95_DMAREQ_LPI2C2_TX = 14,
    FSL_IMX95_DMAREQ_LPI2C2_RX = 15,
    FSL_IMX95_DMAREQ_LPI2C3_TX = 8,
    FSL_IMX95_DMAREQ_LPI2C3_RX = 9,
    FSL_IMX95_DMAREQ_LPI2C4_TX = 10,
    FSL_IMX95_DMAREQ_LPI2C4_RX = 11,
    FSL_IMX95_DMAREQ_LPI2C5_TX = 71,
    FSL_IMX95_DMAREQ_LPI2C5_RX = 72,
    FSL_IMX95_DMAREQ_LPI2C6_TX = 73,
    FSL_IMX95_DMAREQ_LPI2C6_RX = 74,
    FSL_IMX95_DMAREQ_LPI2C7_TX = 75,
    FSL_IMX95_DMAREQ_LPI2C7_RX = 76,
    FSL_IMX95_DMAREQ_LPI2C8_TX = 77,
    FSL_IMX95_DMAREQ_LPI2C8_RX = 78,
};

enum FslImx95MemoryRegions {
    FSL_IMX95_A55_OCRAM,
    FSL_IMX95_A55_TMPSNS,
    FSL_IMX95_ADC,
    FSL_IMX95_AON_MU1_A,
    FSL_IMX95_AON_MU1_B,
    FSL_IMX95_AON_MU1_SRAM_A,
    FSL_IMX95_AON_MU1_SRAM_B,
    FSL_IMX95_AON_MU2_A,
    FSL_IMX95_AON_MU2_B,
    FSL_IMX95_AON_MU2_SRAM_A,
    FSL_IMX95_AON_MU2_SRAM_B,
    FSL_IMX95_AON_MU5_A,
    FSL_IMX95_AON_MU5_B,
    FSL_IMX95_AON_MU5_SRAM_A,
    FSL_IMX95_AON_MU5_SRAM_B,
    FSL_IMX95_BBNSM,
    FSL_IMX95_BLK_CTRL_GPUMIX,
    FSL_IMX95_BLK_CTRL_S_AONMIX,
    FSL_IMX95_BLK_CTRL_WAKEUPMIX1,
    FSL_IMX95_CAMERA_CSR,
    FSL_IMX95_CAMERA_NOC,
    FSL_IMX95_CCM,
    FSL_IMX95_DDRC,
    FSL_IMX95_DDRMIX,
    FSL_IMX95_DISPLAYMIX_IRQSTEER,
    FSL_IMX95_EDMA1,
    FSL_IMX95_EDMA2,
    FSL_IMX95_EDMA3,
    FSL_IMX95_ELE_MU,
    FSL_IMX95_FSB,
    FSL_IMX95_ERM,
    FSL_IMX95_FRACTPLL,
    FSL_IMX95_FRO_TUNER,
    FSL_IMX95_FSPI1_MMAP,
    FSL_IMX95_FSPI1_REG,
    FSL_IMX95_GIC_DIST,
    FSL_IMX95_GIC_ITS,
    FSL_IMX95_GIC_REDIST,
    FSL_IMX95_GPC_CPU_CTRL,
    FSL_IMX95_GPC_GLOBAL,
    FSL_IMX95_GPIO1,
    FSL_IMX95_GPIO2,
    FSL_IMX95_GPIO3,
    FSL_IMX95_GPIO4,
    FSL_IMX95_GPIO5,
    FSL_IMX95_HSIO_BLK_CTRL,
    FSL_IMX95_LPI2C1,
    FSL_IMX95_LPI2C2,
    FSL_IMX95_LPI2C3,
    FSL_IMX95_LPI2C4,
    FSL_IMX95_LPI2C5,
    FSL_IMX95_LPI2C6,
    FSL_IMX95_LPI2C7,
    FSL_IMX95_LPI2C8,
    FSL_IMX95_LPUART_A55,
    FSL_IMX95_LPUART_A55_ALIAS,
    FSL_IMX95_LPUART_SM,
    FSL_IMX95_LPUART_M7,
    FSL_IMX95_M33_CODE_TCM,
    FSL_IMX95_M33_DATA_TCM,
    FSL_IMX95_M33_XCACHE_LPCAC,
    FSL_IMX95_MU3,
    FSL_IMX95_MU4,
    FSL_IMX95_MU6,
    FSL_IMX95_MU8,
    FSL_IMX95_NETCMIX_CTRL,
    FSL_IMX95_NETC_ECAM0,
    FSL_IMX95_NETC_ECAM1,
    FSL_IMX95_NETC_IERB,
    FSL_IMX95_NETC_PRB,
    FSL_IMX95_NOCMIX_BLK_CTRL,
    FSL_IMX95_NOC_CAMERA_BLK_CTRL,
    FSL_IMX95_NOC_SRAMCTL,
    FSL_IMX95_OSC24M,
    FSL_IMX95_PCIE0,
    FSL_IMX95_PCIE0_APP,
    FSL_IMX95_PCIE0_ATU,
    FSL_IMX95_PCIE1,
    FSL_IMX95_PCIE1_APP,
    FSL_IMX95_PCIE1_ATU,
    FSL_IMX95_PERIPH_CATCHALL,
    FSL_IMX95_RAM,
    FSL_IMX95_SAR_ADC,
    FSL_IMX95_SC_MU,
    FSL_IMX95_SMMUV3,
    FSL_IMX95_SRC_GEN,
    FSL_IMX95_SYSCNT_CTRL,
    FSL_IMX95_SYSCNT_CMP,
    FSL_IMX95_SYSCNT_READ,
    FSL_IMX95_TEMPSENSE,
    FSL_IMX95_TMPSNS,
    FSL_IMX95_TPM3,
    FSL_IMX95_TPM6,
    FSL_IMX95_TRDC_AON,
    FSL_IMX95_TRDC_CAMERA,
    FSL_IMX95_TRDC_DISPLAY,
    FSL_IMX95_TRDC_GPU,
    FSL_IMX95_TRDC_HSIO,
    FSL_IMX95_TRDC_MEGA,
    FSL_IMX95_TRDC_NETC,
    FSL_IMX95_TRDC_NOC,
    FSL_IMX95_TRDC_VPU,
    FSL_IMX95_TRDC_WAKEUP,
    FSL_IMX95_USB1,
    FSL_IMX95_USB2,
    FSL_IMX95_USB_CTRL,
    FSL_IMX95_USDHC1,
    FSL_IMX95_USDHC2,
    FSL_IMX95_USDHC3,
    FSL_IMX95_V2X_DBG_MU,
    FSL_IMX95_V2X_MU0,
    FSL_IMX95_V2X_MU3,
    FSL_IMX95_V2X_MU4,
    FSL_IMX95_V2X_MU6,
    FSL_IMX95_VFCCU_FHID0,
    FSL_IMX95_VFCCU_GLOBAL,
    FSL_IMX95_WAKEUP_MU,
    FSL_IMX95_WAKEUP_MU_B,
    FSL_IMX95_WDOG1,
    FSL_IMX95_WDOG2,
    FSL_IMX95_WDOG4,
    FSL_IMX95_WDOG5,
};


#endif /* FSL_IMX95_H */

