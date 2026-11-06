NXP i.MX 95 Evaluation Kit (``imx95-evk``)
==========================================

The ``imx95-evk`` machine models the NXP i.MX 95 19x19 LPDDR5 Evaluation Kit,
based on the i.MX 95 SoC. Like the real board, it starts with the Cortex-M33
System Manager (SM), which then starts the Cortex-A55 cores that run TF-A,
U-Boot and Linux.

Supported devices
-----------------

The ``imx95-evk`` machine implements the following devices:

 * 6 Cortex-A55 cores
 * 1 Cortex-M33 core running the System Manager firmware, with its own NVIC
 * Generic Interrupt Controller (GICv3)
 * 2 LPUARTs (System Manager debug console and Cortex-A55 console)
 * 3 uSDHC Storage Controllers
 * 8 LPI2C Controllers
 * 5 GPIO Controllers
 * 4 32-bit Watchdogs
 * 2 TPM timer/PWM Modules
 * 1 Designware PCI Express Controller
 * 1 Designware USB 3 Controller and 1 ChipIdea USB 2 Controller
 * 1 FlexSPI Controller (register model only, no flash device)
 * 1 ADC
 * System Counter (SYS_CTR)
 * System Reset Controller (SRC), Clock Control Module (CCM) and
   fractional PLLs
 * Message Units for the System Manager, the EdgeLock Enclave (ELE) and V2X
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

The two ``-serial`` options are connected in this order:

 * the first one to the System Manager debug console (a shell with commands
   such as ``reset``)
 * the second one to the Cortex-A55 console (TF-A, U-Boot and Linux)

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

KVM Acceleration
----------------

The ``imx95-evk`` machine has only been run with the default TCG
acceleration. KVM has not been tried: the machine contains a Cortex-M33 core
next to the Cortex-A55 cores, and its boot flow runs TF-A and U-Boot at the
EL3 and EL2 exception levels, which KVM does not provide.
