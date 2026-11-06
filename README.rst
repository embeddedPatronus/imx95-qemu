=========================================
i.MX95 Full-Firmware Boot-Chain Emulation
=========================================

This repository extends QEMU with functional emulation of the NXP i.MX95 SoC and the i.MX95 19x19 LPDDR5 EVK.

The project focuses on executing, observing, and debugging the complete production-style firmware boot chain from a bootable SD or eMMC image:

Bootable WIC image
        |
        v
Behavioral i.MX95 Boot ROM emulation
        |
        | Parse AHAB container sets
        | Load the System Manager and A55 images
        | Create ROM handover and passover data
        v
Cortex-M33 System Manager firmware
        |
        | Initialize platform resources
        | Communicate with ELE
        | Provide SCMI services
        | Program the A55 reset vector
        | Release Cortex-A55 CPU0
        v
Cortex-A55 SPL
        |
        v
TF-A BL31
        |
        +----> OP-TEE BL32
        |
        v
U-Boot BL33
        |
        v
Linux
        |
        v
Yocto userspace

The goal is not only to run Linux on an emulated i.MX95 machine but also to preserve the boot ownership, firmware handovers, communication paths,
CPU-release sequence, and secure-world transitions that exist on the real SoC.

The model is intended for:

1. Firmware integration testing
2. Boot-image and BSP regression testing
3. System Manager and SCMI development
4. TF-A and OP-TEE debugging
5. Secure-world SMP analysis
6. U-Boot and Linux platform development
7. Pre-silicon and hardware-scarce development
8. Education about modern heterogeneous Arm SoCs

=================
Boot ROM Behavior
=================

The proprietary i.MX95 Boot ROM binary is not included or executed.

Instead, QEMU behaviorally emulates the externally visible Boot ROM operations required to start the normal i.MX95 firmware chain
from a bootable image.


Why Boot ROM behavior matters:

Loading individual firmware binaries directly into memory is useful for component testing, but it bypasses several important integration contracts:

1. AHAB container layout
2. Image ownership
3. Image load and entry addresses
4. Primary and secondary container selection
5. Boot-device selection
6. Image-set selection
7. ROM-to-System Manager handover
8. ROM passover information
9. SPL image offsets
10. M33 and A55 release ordering

==================================
Cortex-M33 System Manager Behavior
==================================

The Cortex-M33 System Manager is treated as an executing firmware component.

QEMU does not replace the complete System Manager with a host-side SCMI server. The real System Manager firmware executes on the emulated Cortex-M33
and interacts with modeled i.MX95 hardware.

The System Manager environment includes:

1. Cortex-M33 CPU execution
2. M33 ITCM and DTCM
3. NVIC interrupt delivery
4. System Manager debug console
5. A55-side and M33-side Messaging Unit endpoints
6. Shared-memory transport for SCMI
7. Bidirectional MU doorbells
8. Interrupt signaling between the A55 and M33 domains
9. Selected ELE responses required during initialization
10. Clock, reset, power, and block-control state used by the firmware
11. A55 reset-vector programming
12. CPUWAIT-based Cortex-A55 release

This allows SCMI traffic to be studied as an interaction among real firmware, shared memory, Messaging Units, interrupts, and modeled hardware state.

=====================================
System Manager Controlled A55 Startup
=====================================

Cortex-A55 CPU0 is not simply started when the QEMU machine is created.

The boot CPU follows a System Manager controlled sequence:

Boot ROM behavior starts the M33
        |
        v
System Manager initializes the platform
        |
        v
System Manager programs the A55 reset vector
        |
        v
System Manager clears the CPUWAIT state
        |
        v
QEMU releases Cortex-A55 CPU0
        |
        v
SPL begins execution

This preserves the dependency between System Manager initialization and A55 firmware startup.

=======================
Complete Firmware Chain
=======================

The emulation executes externally built firmware and software binaries:

Cortex-M33 System Manager firmware
Cortex-A55 SPL
TF-A BL31
OP-TEE BL32
U-Boot BL33
Linux
Yocto userspace
QEMU does not reimplement these firmware components.

QEMU provides the processor, memory, interrupt, mailbox, boot-media, reset, clock, power, storage, and peripheral behavior required for the original binaries to execute.

The distinction is important:

Behaviorally emulated:
    Boot ROM operations and modeled hardware

Executed as firmware:
    System Manager
    SPL
    TF-A
    OP-TEE
    U-Boot
    Linux

============
Boot Storage
============

The machine boots from a complete SD or eMMC disk image through the modeled uSDHC path.

The same disk image may contain:

1. AHAB containers
2. System Manager firmware
3. Cortex-A55 SPL
4. TF-A
5. OP-TEE
6. U-Boot
7. Linux kernel
8. Device tree
9. Root filesystem

A Yocto-generated WIC image can therefore be tested as one integrated BSP artifact.

===============
Modeled Devices
===============

The current implementation includes functional models or firmware-facing register support for:

1. Six Cortex-A55 CPUs
2. One Cortex-M33 System Manager CPU
3. GICv3 Distributor, Redistributors, and CPU interfaces
4. Messaging Units
5. SCMI shared memory
6. EdgeLock Enclave Messaging Unit
7. Three uSDHC controllers
8. LPUART controllers
9. LPI2C controllers
10. GPIO controllers
11. PCAL6408A and PCAL6524 GPIO expanders
12. PF09 and PF53 PMIC devices
13. System Counter
14. 32-bit watchdogs
15. TPM timer and PWM modules
16. System Reset Controller
17. Clock Control Module
18. Fractional PLLs
19. Always-on and HSIO block-control registers
20. BBNSM
21. Temperature sensors
22. ADC
23. FlexSPI register interface
24. PCIe register interfaces
25. SMMUv3 global-register interface
26. USB integration
27. NETC ECAM support
28. Camera CSR and NoC register interfaces
29. V2X debug Messaging Unit

Some devices are complete functional models. Other devices model only the registers and state transitions required by the currently supported firmware and Linux boot path.

========================================
Value for Firmware and BSP Professionals
========================================

The full boot chain provides a single environment for validating contracts normally owned by different teams:

Boot-image generation
        |
        v
Boot ROM contract
        |
        v
System Manager firmware
        |
        v
SPL
        |
        v
TF-A and OP-TEE
        |
        v
U-Boot
        |
        v
Linux and userspace

====================
Firmware integration
====================

The model can help investigate questions such as:

1. Did the System Manager receive correct handover information?
2. Did SPL receive correct passover information?
3. Did an SCMI request reach the M33?
4. Did the System Manager provide a valid response?
5. Did the System Manager program the correct A55 reset vector?
6. Did the target CPU leave CPUWAIT?
7. Did TF-A restore the expected context?
8. Did OP-TEE select the correct per-core state?
9. Did Linux mount the intended root filesystem?
10. BSP regression testing

A complete WIC image can be tested as the release artifact.

===========================================
Pre-silicon and hardware-scarce development
===========================================

The model can support development when:

1. i.MX95 boards are not yet available
2. Custom boards are still being designed
3. Available hardware must be shared across teams
4. Remote development is required
5. Reproducible automated testing is preferred
6. Value for Students and Hobbyists

A learner can observe control passing through:

Boot media
    -> Boot ROM behavior
    -> Cortex-M33 System Manager
    -> Cortex-A55 SPL
    -> TF-A at EL3
    -> OP-TEE at Secure EL1
    -> U-Boot
    -> Linux
    -> userspace

========================
Fidelity and Limitations
========================

The following distinctions are important:

1. The System Manager, SPL, TF-A, OP-TEE, U-Boot, and Linux are real firmware or software binaries executing on emulated processors.
2. Boot ROM behavior is implemented as a behavioral model.
3. Some peripherals are functional models.
4. Some peripherals implement only boot-relevant register behavior.
5. Selected ELE responses are emulated.
6. The model is not cycle accurate.
7. The model is not performance accurate.
8. The model does not validate electrical behavior.
9. The model does not implement production secure-boot authentication.
10. The model is not suitable for security certification.
11. GPU, NPU, multimedia, and high-speed I/O performance are not modeled.
12. KVM operation has not been validated.
13. TCG is the primary execution environment.

=============
Building QEMU
=============

This repository uses the standard QEMU build system. QEMU uses a separate build directory so that generated files do not modify the source tree.

Install build dependencies
The exact package names depend on the host operating system. Refer to the official QEMU build-environment documentation
for the current dependency list and distribution-specific instructions:

https://www.qemu.org/docs/master/devel/build-environment.html
https://www.qemu.org/docs/master/devel/index-build.html

For Debian or Ubuntu, install the minimum commonly required packages:

sudo apt update

sudo apt install \
    build-essential \
    git \
    python3 \
    python3-venv \
    ninja-build \
    meson \
    pkg-config \
    libglib2.0-dev \
    libpixman-1-dev \
    libfdt-dev \
    libffi-dev \
    bison \
    flex

-- Fetch this repository

git clone https://github.com/embeddedPatronus/imx95-qemu.git
cd imx95-qemu

-- configure the build

Create a separate build directory and configure QEMU for AArch64 system emulation:

mkdir build
cd build

../configure --target-list=aarch64-softmmu
make -j$(nproc)

============================
Running iMX95 Qemu emulation
============================

cd build

./qemu-system-aarch64 -M imx95-evk -drive if=sd,index=0,file=<path_of .wic>,format=raw -display none  -serial pty -serial stdio


-- Expected logs:-

char device redirected to /dev/pts/11 (label serial0)

U-Boot SPL 2026.04-lf_v2026.04+g6eeef838dac+p0 (Jun 02 2026 - 15:03:39 +0000)
SYS Boot reason: por, origin: -1, errid: -1
Normal Boot
Trying to boot from MMC1
Boot stage: Primary
Image set: 0, offset: 0x8000
Load image from MMC/SD 0xf5800
NOTICE:  BL31: v2.14.1(release):lf-6.18.20-2.0.0
NOTICE:  BL31: Built : 12:42:19, Jun  8 2026


U-Boot 2026.04-lf_v2026.04+g6eeef838dac+p0 (Jun 02 2026 - 15:03:39 +0000)

CPU:   NXP i.MX95 Rev1.0 A55 at 1800 MHz
CPU:   Consumer temperature grade (0C to 95C) - invalid sensor data
Model: NXP i.MX95 19X19 board
...
...



The example command configures two serial consoles:

"-serial pty -serial stdio"
The serial ports are assigned as follows:

Serial option 	                        Console
First serial port, -serial pty	        Cortex-M33 System Manager debug console
Second serial port, -serial stdio	Cortex-A55 firmware and Linux console

When QEMU starts, it prints the pseudo-terminal allocated to the System Manager console. Example:

char device redirected to /dev/pts/11

To access the SM console on another tab:-

screen /dev/pts/11

You can therefore observe both processing domains at the same time:

Terminal 1
    Cortex-A55 console
    SPL, TF-A, OP-TEE, U-Boot and Linux logs

Terminal 2
    Cortex-M33 System Manager console
    System Manager logs and interactive commands



    
===========
QEMU README
===========

QEMU is a generic and open source machine & userspace emulator and
virtualizer.

QEMU is capable of emulating a complete machine in software without any
need for hardware virtualization support. By using dynamic translation,
it achieves very good performance. QEMU can also integrate with the Xen
and KVM hypervisors to provide emulated hardware while allowing the
hypervisor to manage the CPU. With hypervisor support, QEMU can achieve
near native performance for CPUs. When QEMU emulates CPUs directly it is
capable of running operating systems made for one machine (e.g. an ARMv7
board) on a different machine (e.g. an x86_64 PC board).

QEMU is also capable of providing userspace API virtualization for Linux
and BSD kernel interfaces. This allows binaries compiled against one
architecture ABI (e.g. the Linux PPC64 ABI) to be run on a host using a
different architecture ABI (e.g. the Linux x86_64 ABI). This does not
involve any hardware emulation, simply CPU and syscall emulation.

QEMU aims to fit into a variety of use cases. It can be invoked directly
by users wishing to have full control over its behaviour and settings.
It also aims to facilitate integration into higher level management
layers, by providing a stable command line interface and monitor API.
It is commonly invoked indirectly via the libvirt library when using
open source applications such as oVirt, OpenStack and virt-manager.

QEMU as a whole is released under the GNU General Public License,
version 2. For full licensing details, consult the LICENSE file.


Documentation
=============

Documentation can be found hosted online at
`<https://www.qemu.org/documentation/>`_. The documentation for the
current development version that is available at
`<https://www.qemu.org/docs/master/>`_ is generated from the ``docs/``
folder in the source tree, and is built by `Sphinx
<https://www.sphinx-doc.org/en/master/>`_.


Building
========

QEMU is multi-platform software intended to be buildable on all modern
Linux platforms, OS-X, Win32 (via the Mingw64 toolchain) and a variety
of other UNIX targets. The simple steps to build QEMU are:


.. code-block:: shell

  mkdir build
  cd build
  ../configure
  make
