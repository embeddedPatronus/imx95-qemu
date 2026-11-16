NXP i.MX 95 Evaluation Kit (``imx95-evk``)
==========================================

The ``imx95-evk`` machine models the NXP i.MX 95 19x19 LPDDR5 Evaluation Kit,
based on the i.MX 95 SoC. Like the real board, it starts with the Cortex-M33
System Manager (SM), which then starts the Cortex-A55 cores that run TF-A,
U-Boot and Linux, and the Cortex-M7 when its image is part of the boot
medium.

Supported devices
-----------------

The ``imx95-evk`` machine implements the following devices:

 * 6 Cortex-A55 cores
 * 1 Cortex-M33 core running the System Manager firmware, with its own NVIC
 * 1 Cortex-M7 core with its 256 KiB instruction and 256 KiB data TCM
 * Generic Interrupt Controller (GICv3) with LPI support and an Interrupt
   Translation Service (ITS) for message signalled interrupts
 * 3 LPUARTs (System Manager debug console, Cortex-A55 console and
   Cortex-M7 console)
 * 3 uSDHC Storage Controllers
 * 8 LPI2C Controllers
 * 5 GPIO Controllers
 * 4 32-bit Watchdogs
 * 2 TPM timer/PWM Modules
 * 2 PCI Express root complexes (PCIe0 and PCIe1) with the SerDes subsystem
   registers, an unrolled iATU and a requester ID look-up table
 * 1 Designware USB 3 Controller and 1 ChipIdea USB 2 Controller
 * 1 FlexSPI Controller (register model only, no flash device)
 * 1 ADC
 * System Counter (SYS_CTR)
 * System Reset Controller (SRC), Clock Control Module (CCM) and
   fractional PLLs
 * Message Units for the System Manager, the Cortex-M7, the Cortex-A55 to
   Cortex-M7 mailbox, the EdgeLock Enclave (ELE) and V2X
 * Battery-Backed Non-Secure Module (BBNSM) and temperature sensors
 * SMMUv3 (global register front end only)

The board adds the following I2C devices:

 * 4 PCAL6408A GPIO expanders and 1 PCAL6524 GPIO expander
 * 1 PF09 PMIC and 2 PF53 PMICs

Many of the blocks are minimal models. They provide the registers that the
System Manager, U-Boot and Linux access during boot, and not the full
functionality of the hardware.

Memory and consoles
-------------------

The RAM starts at ``0x80000000`` and is at most 16 GiB. Use ``-m`` to select
a smaller size.

The three ``-serial`` options are connected in this order:

 * the first one to the System Manager debug console (a shell with commands
   such as ``reset``)
 * the second one to the Cortex-A55 console (TF-A, U-Boot and Linux)
 * the third one to the Cortex-M7 console (LPUART3)

Boot options
------------

The ``imx95-evk`` machine runs the boot ROM flow itself. It takes the System
Manager and the Cortex-A55 SPL from the AHAB container sets of the boot
medium, as the boot ROM does, and starts the System Manager. The System
Manager then starts the Cortex-A55 cores at the SPL. The standard ``-kernel``
functionality to start a Linux kernel directly is not used.

Boot from an SD card image
~~~~~~~~~~~~~~~~~~~~~~~~~~

The boot medium is the drive given with ``-drive if=sd,index=0``. It must be
a full disk image with the container sets at the usual offset, for example
the ``.wic`` image of an NXP Yocto build (``imx-image-full`` for
``imx95-19x19-lpddr5-evk``). The drive is attached to uSDHC1 as the eMMC
of the board.

.. code-block:: bash

   $ qemu-system-aarch64 -M imx95-evk \
       -display none -serial pty -serial stdio \
       -drive file=imx-image-full-imx95-19x19-lpddr5-evk.wic,if=sd,format=raw

QEMU prints the path of the pseudo terminal of the first serial port
(``char device redirected to /dev/pts/N``).

Use the reported pseudo terminal to access the System Manager console:

.. code-block:: bash

   $ screen /dev/pts/N

Cortex-M7
---------

The Cortex-M7 image is the one of the AHAB container set, as on the real SoC:
the boot ROM loads it into the TCM and lists it in the handover data, and the
System Manager starts it by clearing ``CPUWAIT`` in ``M7_CFG``. The vector
table address is taken from ``INITVTOR``. With a container set that has no
Cortex-M7 image the core stays stopped.

The Cortex-M7 has 16 MPU regions. Stopping and restarting it, its reset
requests to the System Manager and TCM sizes other than 256 KiB and 256 KiB
are not modelled.

PCI Express and message signalled interrupts
--------------------------------------------

Both controllers can host PCI devices. Attach them to the buses
``imx95-pcie0`` and ``imx95-pcie1``:

.. code-block:: bash

   $ qemu-system-aarch64 -M imx95-evk ... \
       -drive file=disk.img,if=none,id=nvm0,format=raw \
       -device nvme,drive=nvm0,serial=0001,bus=imx95-pcie1

Devices signal MSI and MSI-X interrupts through the ITS, as the device tree of
the board describes with ``msi-map``. The SerDes subsystem of each controller
translates the requester ID of the device into the stream ID with its look-up
table, which Linux programs when it enables a device, and the ITS sees that
stream ID with the controller ID in front of it as the DeviceID. A write that
matches no entry of the table is dropped and sets ``PE0_LUT_INT_STS.LUTWMS``.
The Reference Manual does not say what the controller does with the data of
such a write, so the model is free here.

NVMe, virtio-blk, e1000e and XHCI devices have been used with MSI and MSI-X on
both controllers. The legacy interrupts INTA to INTD are wired to the GIC as
the device tree describes, and have not been exercised.

Fuses and the device tree
-------------------------

U-Boot removes the nodes of the blocks that the fuses disable (Cortex-A55
cores, PCIe, GPU, VPU, display and others) from the device tree that it gives
to Linux. It reads the fuses through the ELE. The ELE of the model returns the
fuse words that the System Manager firmware documents as the feature-disable
fuses: all zero by default, which is an unprogrammed part. The machine does not
decide which blocks exist. To see how the software reacts, set a word with
``-global``:

.. code-block:: bash

   # fuse word 19: bit 4 NETC, 6 PCIe controller at 0x4c300000 (PCIe0 here),
   # 7 PCIe controller at 0x4c380000 (PCIe1 here), 17 GPU, 18 VPU disabled
   -global driver=imx95.ele-mu,property=fuse-features,value=0x40
   # fuse word 18: bit 0 NPU, bits 1 to 6 Cortex-A55 core 0 to 5 disabled
   -global driver=imx95.ele-mu,property=fuse-npu-a55,value=0x60

The bit positions come from the fuse map of the System Manager firmware, and
which PCIe controller a bit removes from the device tree from the U-Boot
source (it calls them PCIE A and PCIE B; the System Manager fuse map calls
them PCIE1 and PCIE2). The Reference Manual lists the fuse names but not
these positions.

KVM Acceleration
----------------

The ``imx95-evk`` machine has only been run with the default TCG
acceleration. KVM has not been tried: the machine contains a Cortex-M33 core
next to the Cortex-A55 cores, and its boot flow runs TF-A and U-Boot at the
EL3 and EL2 exception levels, which KVM does not provide.
