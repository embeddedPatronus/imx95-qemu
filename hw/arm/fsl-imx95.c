/*
 * NXP i.MX95 SoC emulation
 *
 * SoC model: six Cortex-A55 cores behind a GICv3, the Cortex-M33 System
 * Manager core with its NVIC, the memory map and the peripherals the
 * System Manager, U-Boot and Linux use during boot, with their interrupt
 * and wake-up wiring. It also registers the reset of the CPU cores.
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/main-loop.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
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
#include "hw/misc/unimp.h"
#include "target/arm/gtimer.h"

static const struct {
    hwaddr addr;
    size_t size;
    const char *name;
} fsl_imx95_memmap[] = {
    [FSL_IMX95_A55_OCRAM] = { 0x20480000, 1 * MiB, "imx95.a55.ocram" },
    [FSL_IMX95_A55_TMPSNS] = { 0x4a440000, 4 * KiB, "imx95.a55-tmpsns" },
    [FSL_IMX95_ADC] = { 0x44530000, 64 * KiB, "imx95.adc" },
    [FSL_IMX95_AON_MU1_A] = { 0x44220000, 64 * KiB, "imx95.aon-mu1-a" },
    [FSL_IMX95_AON_MU1_B] = { 0x44230000, 64 * KiB, "imx95.aon-mu1-b" },
    [FSL_IMX95_AON_MU1_SRAM_A] = { 0x44221000, 4 * KiB, "imx95.aon-mu1-sram" },
    [FSL_IMX95_AON_MU1_SRAM_B] = { 0x44231000, 4 * KiB, "imx95.aon-mu1-sram-b-alias" },
    [FSL_IMX95_AON_MU2_A] = { 0x445b0000, 64 * KiB, "imx95.aon-mu2-a" },
    [FSL_IMX95_AON_MU2_B] = { 0x445c0000, 64 * KiB, "imx95.aon-mu2-b" },
    [FSL_IMX95_AON_MU2_SRAM_A] = { 0x445b1000, 1 * KiB, "imx95.aon-mu2-sram" },
    [FSL_IMX95_AON_MU2_SRAM_B] = { 0x445c1000, 1 * KiB, "imx95.aon-mu2-sram-b-alias" },
    [FSL_IMX95_AON_MU5_B] = { 0x44620000, 64 * KiB, "imx95.aon-mu5-b" },
    [FSL_IMX95_BBNSM] = { 0x44440000, 64 * KiB, "imx95.bbnsm" },
    [FSL_IMX95_BLK_CTRL_GPUMIX] = { 0x4d810000, 64 * KiB, "imx95.blk-ctrl-gpumix" },
    [FSL_IMX95_BLK_CTRL_S_AONMIX] = { 0x444f0000, 64 * KiB, "imx95.blk-ctrl-s-aonmix" },
    [FSL_IMX95_BLK_CTRL_WAKEUPMIX1] = { 0x42420000, 64 * KiB, "imx95.blk-ctrl-wakeupmix1" },
    [FSL_IMX95_CAMERA_CSR] = { 0x4ac10000, 64 * KiB, "imx95.camera-csr" },
    [FSL_IMX95_CAMERA_NOC] = { 0x4aff0000, 64 * KiB, "imx95.camera-noc" },
    [FSL_IMX95_CCM] = { 0x44450000, 64 * KiB, "imx95.ccm" },
    [FSL_IMX95_DDRMIX] = { 0x5d000000, 32 * MiB, "imx95.ddrmix" },
    [FSL_IMX95_DISPLAYMIX_IRQSTEER] = { 0x4b0b0000, 4 * KiB, "imx95.displaymix-irqsteer" },
    [FSL_IMX95_EDMA1] = { 0x44000000, 2 * MiB, "imx95.edma1" },
    [FSL_IMX95_EDMA2] = { 0x42000000, 2112 * KiB, "imx95.edma2" },
    [FSL_IMX95_EDMA3] = { 0x42210000, 2112 * KiB, "imx95.edma3" },
    [FSL_IMX95_ELE_MU] = { 0x47500000, 512 * KiB, "imx95.ele-mu" },
    [FSL_IMX95_ERM] = { 0x44560000, 64 * KiB, "imx95.erm" },
    [FSL_IMX95_FRACTPLL] = { 0x44481000, 4 * KiB, "imx95.fractpll" },
    [FSL_IMX95_FRO_TUNER] = { 0x44485000, 64 * KiB, "imx95.fro-tuner" },
    [FSL_IMX95_FSPI1_MMAP] = { 0x28000000, 128 * MiB, "imx95.fspi1-mmap" },
    [FSL_IMX95_FSPI1_REG] = { 0x425e0000, 64 * KiB, "imx95.fspi1-reg" },
    [FSL_IMX95_GIC_DIST] = { 0x48000000, 64 * KiB, "gic_dist" },
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
    [FSL_IMX95_LPUART_A55_ALIAS] = { 0x5a060000, 64 * KiB, "imx8-lpuart6-alias" },
    [FSL_IMX95_LPUART_SM] = { 0x44390000, 64 * KiB, "imx95.lpuart-sm" },
    [FSL_IMX95_M33_CODE_TCM] = { 0x1ffc0000, 256 * KiB, "imx95.m33.code-tcm" },
    [FSL_IMX95_M33_DATA_TCM] = { 0x20000000, 256 * KiB, "imx95.m33.data-tcm" },
    [FSL_IMX95_M33_XCACHE_LPCAC] = { 0x44400000, 64 * KiB, "imx95.m33-xcache-lpcac" },
    [FSL_IMX95_MU3] = { 0x445d0000, 64 * KiB, "imx95.mu3" },
    [FSL_IMX95_MU4] = { 0x445f0000, 64 * KiB, "imx95.mu4" },
    [FSL_IMX95_MU6] = { 0x44630000, 64 * KiB, "imx95.mu6" },
    [FSL_IMX95_MU8] = { 0x42730000, 64 * KiB, "imx95.mu8" },
    [FSL_IMX95_NETCMIX_CTRL] = { 0x4c810000, 64 * KiB, "imx95.netcmix-ctrl" },
    [FSL_IMX95_NETC_ECAM0] = { 0x4ca00000, 1 * MiB, "imx95.netc-ecam0" },
    [FSL_IMX95_NETC_ECAM1] = { 0x4cb00000, 1 * MiB, "imx95.netc-ecam1" },
    [FSL_IMX95_NETC_IERB] = { 0x4cde0000, 64 * KiB, "imx95.netc-ierb" },
    [FSL_IMX95_NETC_PRB] = { 0x4cdf0000, 64 * KiB, "imx95.netc-prb" },
    [FSL_IMX95_NOCMIX_BLK_CTRL] = { 0x49000000, 64 * KiB, "imx95.nocmix-blk-ctrl" },
    [FSL_IMX95_NOC_CAMERA_BLK_CTRL] = { 0x49062000, 64 * KiB, "imx95.noc-camera-blk-ctrl" },
    [FSL_IMX95_NOC_SRAMCTL] = { 0x490a0000, 64 * KiB, "imx95.noc-sramctl" },
    [FSL_IMX95_OSC24M] = { 0x44480000, 4 * KiB, "imx95.osc24m" },
    [FSL_IMX95_PCIE0_APP] = { 0x4c340000, 16 * KiB, "imx95.pcie0-app" },
    [FSL_IMX95_PCIE1] = { 0x4c380000, 64 * KiB, "imx95.pcie1" },
    [FSL_IMX95_PCIE1_APP] = { 0x4c3c0000, 16 * KiB, "imx95.pcie1-app" },
    [FSL_IMX95_PERIPH_CATCHALL] = { 0x40000000, 256 * MiB, "imx95.periph-catchall" },
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
    [FSL_IMX95_USDHC3] = { 0x42870000, 64 * KiB, "imx95.usdhc3" },
    [FSL_IMX95_V2X_DBG_MU] = { 0x47350000, 64 * KiB, "imx95.v2x-dbg-mu" },
    [FSL_IMX95_V2X_MU0] = { 0x47300000, 64 * KiB, "imx95.v2x-mu0" },
    [FSL_IMX95_V2X_MU3] = { 0x47330000, 64 * KiB, "imx95.v2x-mu3" },
    [FSL_IMX95_V2X_MU4] = { 0x47340000, 64 * KiB, "imx95.v2x-mu4" },
    [FSL_IMX95_V2X_MU6] = { 0x47320000, 64 * KiB, "imx95.v2x-mu6" },
    [FSL_IMX95_VFCCU_FHID0] = { 0x446b0000, 64 * KiB, "imx95.vfccu-fhid0" },
    [FSL_IMX95_VFCCU_GLOBAL] = { 0x44570000, 64 * KiB, "imx95.vfccu-global" },
    [FSL_IMX95_WAKEUP_MU] = { 0x42430000, 64 * KiB, "imx95.wakeup-mu" },
    [FSL_IMX95_WDOG1] = { 0x442d0000, 64 * KiB, "imx95.wdog1" },
    [FSL_IMX95_WDOG2] = { 0x442e0000, 64 * KiB, "imx95.wdog2" },
    [FSL_IMX95_WDOG4] = { 0x42490000, 64 * KiB, "imx95.wdog4" },
    [FSL_IMX95_WDOG5] = { 0x424a0000, 64 * KiB, "imx95.wdog5" },
};

static void fsl_imx95_init(Object *obj)
{
    FslImx95State *s = FSL_IMX95(obj);
    int i;

    object_initialize_child(obj, "a55-cluster", &s->a55_cluster,
                            TYPE_CPU_CLUSTER);
    object_initialize_child(obj, "gic", &s->gic, gicv3_class_name());
    object_initialize_child(obj, "m33", &s->m33, TYPE_ARMV7M);

    for (i = 0; i < ARRAY_SIZE(s->aon_mu); i++) {
        g_autofree char *name = g_strdup_printf("aon-mu%d", i + 1);
        object_initialize_child(obj, name, &s->aon_mu[i], TYPE_IMX95_MU);
    }
    object_initialize_child(obj, "wakeup-mu", &s->wakeup_mu, TYPE_IMX95_MU);

    for (i = 0; i < FSL_IMX95_NUM_LPUARTS; i++) {
        g_autofree char *name = g_strdup_printf("lpuart%d", i + 1);
        object_initialize_child(obj, name, &s->lpuart[i], TYPE_IMX_LPUART);
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
    object_initialize_child(obj, "sysctr", &s->sysctr, TYPE_IMX95_SYSCTR);
    for (i = 0; i < 2; i++) {
        g_autofree char *nm = g_strdup_printf("pcie-serdes%d", i);
        object_initialize_child(obj, nm, &s->pcie_serdes[i], TYPE_IMX95_PCIE_SERDES_SS);
        g_free(nm); nm = g_strdup_printf("netc-ecam%d", i);
        object_initialize_child(obj, nm, &s->netc_ecam[i], TYPE_IMX95_NETC_ECAM);
    }
    object_initialize_child(obj, "hsio-blk-ctrl", &s->hsio_blk_ctrl, TYPE_IMX95_HSIO_BLK_CTRL);
    object_initialize_child(obj, "camera-csr", &s->camera_csr, TYPE_IMX95_CAMERA_CSR);
    object_initialize_child(obj, "camera-noc", &s->camera_noc, TYPE_IMX95_CAMERA_NOC);
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
    object_initialize_child(obj, "pcie1", &s->pcie1,
                            TYPE_DESIGNWARE_PCIE_HOST);
}

/*
 * CPU objects are not part of the device reset tree, so each board has to
 * register their reset. All Cortex-A55 cores are created "start-powered-off":
 * a reset puts them back in that state and the System Manager releases them
 * again (CPUWAIT / RVBARADDR), as it does after a power-on.
 */
static void fsl_imx95_a55_reset(void *opaque)
{
    cpu_reset(CPU(opaque));
}

static void fsl_imx95_realize(DeviceState *dev, Error **errp)
{
    FslImx95State *s = FSL_IMX95(dev);
    MemoryRegion *sysmem = get_system_memory();
    DeviceState *gicdev = DEVICE(&s->gic);
    DeviceState *m33dev = DEVICE(&s->m33);
    int i;

    /* On-chip memories */
    if (!memory_region_init_ram(&s->m33_code_tcm, OBJECT(dev),
                                fsl_imx95_memmap[FSL_IMX95_M33_CODE_TCM].name,
                                fsl_imx95_memmap[FSL_IMX95_M33_CODE_TCM].size,
                                errp)) {
        return;
    }
    memory_region_add_subregion(sysmem,
                                fsl_imx95_memmap[FSL_IMX95_M33_CODE_TCM].addr,
                                &s->m33_code_tcm);

    if (!memory_region_init_ram(&s->m33_data_tcm, OBJECT(dev),
                                fsl_imx95_memmap[FSL_IMX95_M33_DATA_TCM].name,
                                fsl_imx95_memmap[FSL_IMX95_M33_DATA_TCM].size,
                                errp)) {
        return;
    }
    memory_region_add_subregion(sysmem,
                                fsl_imx95_memmap[FSL_IMX95_M33_DATA_TCM].addr,
                                &s->m33_data_tcm);

    /* Holds the A55 SPL image placed there by the boot ROM. */
    if (!memory_region_init_ram(&s->a55_ocram, OBJECT(dev),
                                fsl_imx95_memmap[FSL_IMX95_A55_OCRAM].name,
                                fsl_imx95_memmap[FSL_IMX95_A55_OCRAM].size,
                                errp)) {
        return;
    }
    memory_region_add_subregion(sysmem,
                                fsl_imx95_memmap[FSL_IMX95_A55_OCRAM].addr,
                                &s->a55_ocram);

    /*
     * U-Boot reads and writes DDR controller registers while it runs. SPL
     * has already initialised the real DDR; plain RAM is enough here.
     */
    if (!memory_region_init_ram(&s->ddrmix, OBJECT(dev),
                                fsl_imx95_memmap[FSL_IMX95_DDRMIX].name,
                                fsl_imx95_memmap[FSL_IMX95_DDRMIX].size,
                                errp)) {
        return;
    }
    memory_region_add_subregion(sysmem,
                                fsl_imx95_memmap[FSL_IMX95_DDRMIX].addr,
                                &s->ddrmix);

    /* SC firmware MU (i.MX8-style), overlays DDRMIX. */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->sc_mu), errp)) {
        return;
    }
    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(&s->sc_mu), 0,
                            fsl_imx95_memmap[FSL_IMX95_SC_MU].addr, 1);

    /* Cortex-A55 cores */
    qdev_prop_set_uint32(DEVICE(&s->a55_cluster), "cluster-id", 0);
    for (i = 0; i < FSL_IMX95_NUM_CPUS; i++) {
        g_autofree char *name = g_strdup_printf("cpu%d", i);
        uint64_t affinity = (uint64_t)i << 8;

        object_initialize_child(OBJECT(&s->a55_cluster), name, &s->cpu[i],
                                ARM_CPU_TYPE_NAME("cortex-a55"));

        /*
         * The device tree identifies the cores through Aff1 = 0..5 with
         * Aff0 = 0. Firmware's CPU topology code needs MPIDR_EL1.MT set
         * for that layout.
         */
        s->cpu[i].mpidr_mt = true;
        object_property_set_uint(OBJECT(&s->cpu[i]), "mp-affinity", affinity,
                                 &error_abort);
        object_property_set_uint(OBJECT(&s->cpu[i]), "cntfrq", 24000000,
                                 &error_abort);
        /* All cores start powered off; the System Manager releases them. */
        object_property_set_bool(OBJECT(&s->cpu[i]), "start-powered-off",
                                 true, &error_abort);

        if (!qdev_realize(DEVICE(&s->cpu[i]), NULL, errp)) {
            return;
        }
        qemu_register_reset(fsl_imx95_a55_reset, &s->cpu[i]);
    }
    /* Realizing the cluster requires at least one realized CPU. */
    if (!qdev_realize(DEVICE(&s->a55_cluster), NULL, errp)) {
        return;
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
        redist_region_count = qlist_new();
        qlist_append_int(redist_region_count, FSL_IMX95_NUM_CPUS);
        qdev_prop_set_array(gicdev, "redist-region-count",
                            redist_region_count);
        object_property_set_link(OBJECT(&s->gic), "sysmem", OBJECT(sysmem),
                                 &error_abort);
        if (!sysbus_realize(gicsbd, errp)) {
            return;
        }
        sysbus_mmio_map(gicsbd, 0, fsl_imx95_memmap[FSL_IMX95_GIC_DIST].addr);
        sysbus_mmio_map(gicsbd, 1,
                        fsl_imx95_memmap[FSL_IMX95_GIC_REDIST].addr);

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
            /* wake request (GIC -> power controller), power-down WFI (CPU -> power controller) */
            sysbus_connect_irq(gicsbd, i + 6 * FSL_IMX95_NUM_CPUS,
                qdev_get_gpio_in_named(DEVICE(&s->blk_ctrl_aonmix),
                                       "gic-wakeup-req", i));
            qdev_connect_gpio_out_named(cpudev, "wfi-powerdown", 0,
                qdev_get_gpio_in_named(DEVICE(&s->blk_ctrl_aonmix),
                                       "a55-wfi-powerdown", i));
        }
    }

    /* Cortex-M33 (System Manager) with its NVIC */
    s->m33_cpuclk = clock_new(OBJECT(dev), "m33-cpuclk");
    s->m33_refclk = clock_new(OBJECT(dev), "m33-refclk");
    clock_set_hz(s->m33_cpuclk, 24 * 1000 * 1000);
    clock_set_hz(s->m33_refclk, 24 * 1000 * 1000);
    qdev_connect_clock_in(m33dev, "cpuclk", s->m33_cpuclk);
    qdev_connect_clock_in(m33dev, "refclk", s->m33_refclk);
    object_property_set_link(OBJECT(&s->m33), "memory", OBJECT(sysmem),
                             &error_abort);
    qdev_prop_set_string(m33dev, "cpu-type", ARM_CPU_TYPE_NAME("cortex-m33"));
    qdev_prop_set_uint32(m33dev, "num-irq", FSL_IMX95_M33_NUM_IRQS);
    /* Reset fetches the initial MSP and PC from the start of the code TCM. */
    qdev_prop_set_uint32(m33dev, "init-svtor",
                         fsl_imx95_memmap[FSL_IMX95_M33_CODE_TCM].addr);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->m33), errp)) {
        return;
    }
    g_assert(s->m33.cpu != NULL);
    CPU(s->m33.cpu)->cluster_index = 1;
    /*
     * Every M-profile board has to call this: with no kernel it only
     * registers the reset of the Cortex-M33, which then reloads its stack
     * pointer and reset vector from the System Manager image (restored by the
     * ROM loader) and runs it again.
     */
    armv7m_load_kernel(s->m33.cpu, NULL, 0, 0);

    /* AON message units (A side -> GIC, B side -> M33 NVIC) */
    for (i = 0; i < ARRAY_SIZE(s->aon_mu); i++) {
        static const struct {
            int a, b, sram_a, sram_b;
            unsigned int mua_irq, mub_irq;
        } mu_table[ARRAY_SIZE(s->aon_mu)] = {
            { FSL_IMX95_AON_MU1_A, FSL_IMX95_AON_MU1_B,
              FSL_IMX95_AON_MU1_SRAM_A, FSL_IMX95_AON_MU1_SRAM_B,
              FSL_IMX95_AON_MU1_MUA_IRQ, FSL_IMX95_AON_MU1_MUB_M33_IRQ },
            { FSL_IMX95_AON_MU2_A, FSL_IMX95_AON_MU2_B,
              FSL_IMX95_AON_MU2_SRAM_A, FSL_IMX95_AON_MU2_SRAM_B,
              FSL_IMX95_AON_MU2_MUA_IRQ, FSL_IMX95_AON_MU2_MUB_M33_IRQ },
        };
        SysBusDevice *mu = SYS_BUS_DEVICE(&s->aon_mu[i]);

        /*
         * Communication SRAM shared by MUA and MUB. BL31 keeps its AP/SM
         * semaphore at the end of the MU1 window.
         */
        if (!memory_region_init_ram(&s->aon_mu_sram[i], OBJECT(dev),
                                    fsl_imx95_memmap[mu_table[i].sram_a].name,
                                    fsl_imx95_memmap[mu_table[i].sram_a].size,
                                    errp)) {
            return;
        }
        memory_region_add_subregion(sysmem,
                                    fsl_imx95_memmap[mu_table[i].sram_a].addr,
                                    &s->aon_mu_sram[i]);
        memory_region_init_alias(&s->aon_mu_sram_b_alias[i], OBJECT(dev),
                                 fsl_imx95_memmap[mu_table[i].sram_b].name,
                                 &s->aon_mu_sram[i], 0,
                                 fsl_imx95_memmap[mu_table[i].sram_b].size);
        memory_region_add_subregion(sysmem,
                                    fsl_imx95_memmap[mu_table[i].sram_b].addr,
                                    &s->aon_mu_sram_b_alias[i]);

        if (!sysbus_realize(mu, errp)) {
            return;
        }
        sysbus_mmio_map(mu, 0, fsl_imx95_memmap[mu_table[i].a].addr);
        sysbus_mmio_map(mu, 1, fsl_imx95_memmap[mu_table[i].b].addr);
        sysbus_connect_irq(mu, 0, qdev_get_gpio_in(gicdev, mu_table[i].mua_irq));
        sysbus_connect_irq(mu, 1, qdev_get_gpio_in(m33dev, mu_table[i].mub_irq));
        s->aon_mu[i].gier[0] = BIT(0);
        s->aon_mu[i].gier[1] = BIT(0);
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->wakeup_mu), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->wakeup_mu), 0,
                    fsl_imx95_memmap[FSL_IMX95_WAKEUP_MU].addr);

    /* LPUARTs: SM console and aux go to the M33, the A55 console to the GIC */
    for (i = 0; i < FSL_IMX95_NUM_LPUARTS; i++) {
        static const struct {
            int region;
            unsigned int serial;
            unsigned int irq;
            bool to_gic;
        } lpuart_table[FSL_IMX95_NUM_LPUARTS] = {
            { FSL_IMX95_LPUART_SM,  0, FSL_IMX95_LPUART_SM_IRQ,  false },
            { FSL_IMX95_LPUART_A55, 1, FSL_IMX95_LPUART_A55_IRQ, true },
        };
        Chardev *chr = serial_hd(lpuart_table[i].serial);
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->lpuart[i]);

        if (chr) {
            qdev_prop_set_chr(DEVICE(&s->lpuart[i]), "chardev", chr);
        }
        if (!sysbus_realize(sbd, errp)) {
            return;
        }
        sysbus_mmio_map(sbd, 0, fsl_imx95_memmap[lpuart_table[i].region].addr);
        sysbus_connect_irq(sbd, 0,
            qdev_get_gpio_in(lpuart_table[i].to_gic ? gicdev : m33dev,
                             lpuart_table[i].irq));
    }

    /*
     * The i.MX8-style LPUART6 address is what the shipped U-Boot uses as its
     * console; alias it onto the A55 console UART.
     */
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

    /* LPI2C: LPI2C1 belongs to the M33, the others signal the GIC */
    for (i = 0; i < FSL_IMX95_NUM_LPI2CS; i++) {
        static const struct {
            int region;
            unsigned int irq;
        } lpi2c_table[FSL_IMX95_NUM_LPI2CS] = {
            { FSL_IMX95_LPI2C1, FSL_IMX95_LPI2C1_IRQ },
            { FSL_IMX95_LPI2C2, FSL_IMX95_LPI2C2_IRQ },
            { FSL_IMX95_LPI2C3, FSL_IMX95_LPI2C3_IRQ },
            { FSL_IMX95_LPI2C4, FSL_IMX95_LPI2C4_IRQ },
            { FSL_IMX95_LPI2C5, FSL_IMX95_LPI2C5_IRQ },
            { FSL_IMX95_LPI2C6, FSL_IMX95_LPI2C6_IRQ },
            { FSL_IMX95_LPI2C7, FSL_IMX95_LPI2C7_IRQ },
            { FSL_IMX95_LPI2C8, FSL_IMX95_LPI2C8_IRQ },
        };
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->lpi2c[i]);

        if (!sysbus_realize(sbd, errp)) {
            return;
        }
        sysbus_mmio_map(sbd, 0, fsl_imx95_memmap[lpi2c_table[i].region].addr);
        sysbus_connect_irq(sbd, 0,
            qdev_get_gpio_in(i == 0 ? m33dev : gicdev, lpi2c_table[i].irq));
    }

    /* uSDHCs */
    for (i = 0; i < FSL_IMX95_NUM_USDHCS; i++) {
        static const struct {
            int region;
            unsigned int irq;
        } usdhc_table[FSL_IMX95_NUM_USDHCS] = {
            { FSL_IMX95_USDHC1, FSL_IMX95_USDHC1_IRQ },
            { FSL_IMX95_USDHC2, FSL_IMX95_USDHC2_IRQ },
            { FSL_IMX95_USDHC3, FSL_IMX95_USDHC3_IRQ },
        };

        if (!sysbus_realize(SYS_BUS_DEVICE(&s->usdhc[i]), errp)) {
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->usdhc[i]), 0,
                        fsl_imx95_memmap[usdhc_table[i].region].addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->usdhc[i]), 0,
                           qdev_get_gpio_in(gicdev, usdhc_table[i].irq));
    }

    /* USB: USB1 is DWC3 + xHCI, USB2 is ChipIdea EHCI */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->usb1), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->usb1), 0,
                    fsl_imx95_memmap[FSL_IMX95_USB1].addr);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->usb2), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->usb2), 0,
                    fsl_imx95_memmap[FSL_IMX95_USB2].addr);

    /* PCIe1 is the controller the board device tree enables */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->pcie1), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->pcie1), 0,
                    fsl_imx95_memmap[FSL_IMX95_PCIE1].addr);

    /* Register models */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->tpm[0]), errp)) {
        return;
    }
    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(&s->tpm[0]), 0,
                            fsl_imx95_memmap[FSL_IMX95_TPM3].addr, 1);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->tpm[1]), errp)) {
        return;
    }
    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(&s->tpm[1]), 0,
                            fsl_imx95_memmap[FSL_IMX95_TPM6].addr, 1);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->wdog[0]), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->wdog[0]), 0, fsl_imx95_memmap[FSL_IMX95_WDOG1].addr);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->wdog[1]), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->wdog[1]), 0, fsl_imx95_memmap[FSL_IMX95_WDOG2].addr);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->wdog[2]), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->wdog[2]), 0, fsl_imx95_memmap[FSL_IMX95_WDOG4].addr);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->wdog[3]), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->wdog[3]), 0, fsl_imx95_memmap[FSL_IMX95_WDOG5].addr);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gpio[0]), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpio[0]), 0, fsl_imx95_memmap[FSL_IMX95_GPIO1].addr);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gpio[1]), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpio[1]), 0, fsl_imx95_memmap[FSL_IMX95_GPIO2].addr);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gpio[2]), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpio[2]), 0, fsl_imx95_memmap[FSL_IMX95_GPIO3].addr);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gpio[3]), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpio[3]), 0, fsl_imx95_memmap[FSL_IMX95_GPIO4].addr);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gpio[4]), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpio[4]), 0, fsl_imx95_memmap[FSL_IMX95_GPIO5].addr);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->adc), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->adc), 0, fsl_imx95_memmap[FSL_IMX95_ADC].addr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->adc), 0,
                       qdev_get_gpio_in(gicdev, FSL_IMX95_ADC_IRQ));
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->src_gen), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->src_gen), 0, fsl_imx95_memmap[FSL_IMX95_SRC_GEN].addr);
    /* Reset of a Cortex-A55 core (0-5) or of the A55 platform (6) stops the core */
    for (i = 0; i < FSL_IMX95_NUM_CPUS + 1; i++) {
        qdev_connect_gpio_out_named(DEVICE(&s->src_gen), "a55-hold", i,
            qdev_get_gpio_in_named(DEVICE(&s->blk_ctrl_aonmix), "a55-hold", i));
    }
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->fspi), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->fspi), 0, fsl_imx95_memmap[FSL_IMX95_FSPI1_REG].addr);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->smmuv3), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->smmuv3), 0, fsl_imx95_memmap[FSL_IMX95_SMMUV3].addr);
    /*
     * SMMUv3 PMCGs: the TCU one sits inside the TCU register space, the 11 TBU
     * ones at page 0 offset 0x2000 of each TBU (RM memory map, TBU pages).
     */
    for (i = 0; i < IMX95_SMMU_PMCG_NUM; i++) {
        hwaddr base = i == 0 ? 0x490d2000 : 0x49112000 + (i - 1) * 0x20000;

        if (!sysbus_realize(SYS_BUS_DEVICE(&s->smmu_pmcg[i]), errp)) {
            return;
        }
        /* priority 1: the TCU PMCG overlays the smmuv3 register stub */
        sysbus_mmio_map_overlap(SYS_BUS_DEVICE(&s->smmu_pmcg[i]), 0, base, 1);
    }
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->tmpsns), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->tmpsns), 0, fsl_imx95_memmap[FSL_IMX95_A55_TMPSNS].addr);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->fractpll), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->fractpll), 0, fsl_imx95_memmap[FSL_IMX95_FRACTPLL].addr);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->bbnsm), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->bbnsm), 0, fsl_imx95_memmap[FSL_IMX95_BBNSM].addr);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->ccm), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ccm), 0, fsl_imx95_memmap[FSL_IMX95_CCM].addr);

    /* System counter: the shared 24 MHz time base of the A55 and the SM */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->sysctr), errp)) {
        return;
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
        static const int serdes_id[2] = { FSL_IMX95_PCIE0_APP, FSL_IMX95_PCIE1_APP };
        static const int ecam_id[2] = { FSL_IMX95_NETC_ECAM0, FSL_IMX95_NETC_ECAM1 };

        if (!sysbus_realize(SYS_BUS_DEVICE(&s->pcie_serdes[i]), errp)) {
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->pcie_serdes[i]), 0,
                        fsl_imx95_memmap[serdes_id[i]].addr);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->netc_ecam[i]), errp)) {
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->netc_ecam[i]), 0,
                        fsl_imx95_memmap[ecam_id[i]].addr);
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->hsio_blk_ctrl), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->hsio_blk_ctrl), 0,
                    fsl_imx95_memmap[FSL_IMX95_HSIO_BLK_CTRL].addr);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->camera_csr), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->camera_csr), 0,
                    fsl_imx95_memmap[FSL_IMX95_CAMERA_CSR].addr);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->camera_noc), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->camera_noc), 0,
                    fsl_imx95_memmap[FSL_IMX95_CAMERA_NOC].addr);
    /*
     * BLK_CTRL_S_AONMIX releases the Cortex-A55 cores on behalf of the System
     * Manager and gates the ELE MU3 interrupt through CA55_IRQ_MASK0.
     */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->blk_ctrl_aonmix), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->blk_ctrl_aonmix), 0,
                    fsl_imx95_memmap[FSL_IMX95_BLK_CTRL_S_AONMIX].addr);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->ele_mu), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ele_mu), 0,
                    fsl_imx95_memmap[FSL_IMX95_ELE_MU].addr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->ele_mu), 0,
                       qdev_get_gpio_in(gicdev, FSL_IMX95_ELE_MU3_IRQ));
    qdev_connect_gpio_out_named(DEVICE(&s->blk_ctrl_aonmix), "ele-mu3-irq-mask",
                                0, qdev_get_gpio_in_named(DEVICE(&s->ele_mu),
                                                          "ca55-irq-mask", 0));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->v2x_dbg_mu), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->v2x_dbg_mu), 0,
                    fsl_imx95_memmap[FSL_IMX95_V2X_DBG_MU].addr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->v2x_dbg_mu), 0,
                       qdev_get_gpio_in(gicdev, FSL_IMX95_V2X_DBG_MU_IRQ));


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
        case FSL_IMX95_BBNSM:
        case FSL_IMX95_BLK_CTRL_S_AONMIX:
        case FSL_IMX95_CAMERA_CSR:
        case FSL_IMX95_CAMERA_NOC:
        case FSL_IMX95_CCM:
        case FSL_IMX95_HSIO_BLK_CTRL:
        case FSL_IMX95_DDRMIX:
        case FSL_IMX95_ELE_MU:
        case FSL_IMX95_FRACTPLL:
        case FSL_IMX95_FSPI1_REG:
        case FSL_IMX95_GIC_DIST:
        case FSL_IMX95_GIC_REDIST:
        case FSL_IMX95_GPIO1 ... FSL_IMX95_GPIO5:
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
        case FSL_IMX95_PCIE0_APP:
        case FSL_IMX95_PCIE1:
        case FSL_IMX95_PCIE1_APP:
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
        case FSL_IMX95_WDOG1:
        case FSL_IMX95_WDOG2:
        case FSL_IMX95_WDOG4:
        case FSL_IMX95_WDOG5:
            /* device implemented and treated above */
            break;

        default:
            create_unimplemented_device(fsl_imx95_memmap[i].name,
                                        fsl_imx95_memmap[i].addr,
                                        fsl_imx95_memmap[i].size);
            break;
        }
    }
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
