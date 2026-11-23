/*
 * NXP i.MX95 19x19 Evaluation Kit System Emulation
 *
 * Copyright (c) 2026 Gaurav Sharma <gaurav.sharma0394@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "system/address-spaces.h"
#include "system/block-backend.h"
#include "system/blockdev.h"
#include "hw/arm/fsl-imx95.h"
#include "hw/arm/imx95-bootrom.h"
#include "hw/core/boards.h"
#include "hw/core/qdev-properties.h"
#include "hw/gpio/pcal6408a.h"
#include "hw/gpio/pcal6524.h"
#include "hw/i2c/i2c.h"
#include "hw/misc/pf09_pmic.h"
#include "hw/misc/pf53_pmic.h"
#include "hw/sd/sd.h"

#define TYPE_IMX95_EVK_MACHINE MACHINE_TYPE_NAME("imx95-evk")
OBJECT_DECLARE_SIMPLE_TYPE(FslImx95EvkState, IMX95_EVK_MACHINE)

struct FslImx95EvkState {
    MachineState parent_obj;

    FslImx95State soc;
};

#define IMX95_EVK_PCAL6408A_ADDR    0x20
#define IMX95_EVK_PF09_ADDR         0x08
#define IMX95_EVK_PF5301_ADDR       0x2a
#define IMX95_EVK_PF5302_ADDR       0x29
#define IMX95_EVK_I2C3_TCA6408_ADDR 0x20
#define IMX95_EVK_I2C4_TCA6408_ADDR 0x21
#define IMX95_EVK_I2C5_TCA6408_ADDR 0x21
#define IMX95_EVK_I2C7_PCAL6524_ADDR 0x22


static I2CBus *imx95_evk_i2c_bus(FslImx95EvkState *s,
                                    unsigned int index)
{
    return fsl_imx95_get_i2c_bus(&s->soc, index);
}

static void imx95_evk_init(MachineState *machine)
{
    FslImx95EvkState *s = IMX95_EVK_MACHINE(machine);
    MemoryRegion *sysmem = get_system_memory();
    Imx95BootRomInfo bootrom;
    DriveInfo *di;

    if (machine->ram_size > FSL_IMX95_RAM_SIZE_MAX) {
        error_report("RAM size " RAM_ADDR_FMT " above max supported (%08" PRIx64 ")",
                     machine->ram_size, (uint64_t)FSL_IMX95_RAM_SIZE_MAX);
        exit(1);
    }

    object_initialize_child(OBJECT(machine), "soc", &s->soc, TYPE_FSL_IMX95);
    qdev_prop_set_uint64(DEVICE(&s->soc.ddrc), "dram-base",
                         FSL_IMX95_RAM_START);
    qdev_prop_set_uint64(DEVICE(&s->soc.ddrc), "dram-size",
                         machine->ram_size);

    di = drive_get(IF_SD, 0, 0);
    imx95_bootrom_load(&bootrom, di ? blk_by_legacy_dinfo(di) : NULL,
                       machine->kernel_filename,
                       object_property_get_uint(OBJECT(&s->soc.ele_mu),
                                                "fuse-boot-cfg8",
                                                &error_abort),
                       &error_fatal);
    imx95_bootrom_add_handover(&bootrom);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(&s->soc), &error_fatal);

    memory_region_add_subregion(sysmem, FSL_IMX95_RAM_START, machine->ram);

    /* The SD image appears as the eMMC on USDHC1 */
    if (di) {
        BlockBackend *blk = blk_by_legacy_dinfo(di);
        BusState *bus = fsl_imx95_get_usdhc_bus(&s->soc, 0);
        DeviceState *card = qdev_new(TYPE_EMMC);

        qdev_prop_set_drive_err(card, "drive", blk, &error_fatal);
        qdev_realize_and_unref(card, bus, &error_fatal);
    }

    i2c_slave_create_simple(imx95_evk_i2c_bus(s, 0), TYPE_PCAL6408A,
                            IMX95_EVK_PCAL6408A_ADDR);
    i2c_slave_create_simple(imx95_evk_i2c_bus(s, 0), TYPE_PF09_PMIC,
                            IMX95_EVK_PF09_ADDR);
    i2c_slave_create_simple(imx95_evk_i2c_bus(s, 0), TYPE_PF53_PMIC,
                            IMX95_EVK_PF5301_ADDR);
    i2c_slave_create_simple(imx95_evk_i2c_bus(s, 0), TYPE_PF53_PMIC,
                            IMX95_EVK_PF5302_ADDR);
    i2c_slave_create_simple(imx95_evk_i2c_bus(s, 2), TYPE_PCAL6408A,
                            IMX95_EVK_I2C3_TCA6408_ADDR);
    i2c_slave_create_simple(imx95_evk_i2c_bus(s, 3), TYPE_PCAL6408A,
                            IMX95_EVK_I2C4_TCA6408_ADDR);
    i2c_slave_create_simple(imx95_evk_i2c_bus(s, 4), TYPE_PCAL6408A,
                            IMX95_EVK_I2C5_TCA6408_ADDR);
    i2c_slave_create_simple(imx95_evk_i2c_bus(s, 6), TYPE_PCAL6524,
                            IMX95_EVK_I2C7_PCAL6524_ADDR);
}

static void imx95_evk_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "NXP i.MX95 19x19 EVK Board";
    mc->init = imx95_evk_init;
    mc->max_cpus = FSL_IMX95_NUM_CPUS + 2;
    mc->default_cpus = FSL_IMX95_NUM_CPUS + 2;
    mc->default_ram_id = "imx95-evk.ram";
    mc->default_ram_size = FSL_IMX95_RAM_SIZE_MAX;
}

static const TypeInfo imx95_evk_machine_types[] = {
    {
        .name = TYPE_IMX95_EVK_MACHINE,
        .parent = TYPE_MACHINE,
        .class_init = imx95_evk_machine_class_init,
        .instance_size = sizeof(FslImx95EvkState),
    },
};

DEFINE_TYPES(imx95_evk_machine_types)


