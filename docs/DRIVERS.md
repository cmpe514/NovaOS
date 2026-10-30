# Driver matrix

Statuses: **implemented** (written, and its behaviour tested as stated), **emulated** (works only with
QEMU's virtual hardware), **experimental**, **unsupported**, **blocked**.

| device | target | implementation | dependencies | tested behavior | status | blocker |
|---|---|---|---|---|---|---|
| 16550 UART (COM1) | qemu-x86_64 | `drivers/pc/serial.c`, polled TX | — | every marker in `make test` goes through it | implemented | — |
| 8259A PIC | qemu-x86_64 | `drivers/pc/pic_pit.c` | — | timer and keyboard IRQs delivered | implemented | APIC/IOAPIC not done (needed for SMP, MSI) |
| 8254 PIT | qemu-x86_64 | `drivers/pc/pic_pit.c`, 250 Hz | PIC | preemption, `sleep`, uptime in Settings | implemented | — |
| Exceptions / IDT | qemu-x86_64 | `arch/x86_64/cpu.c`, `isr.S` | — | user page faults, #GP and NX faults kill only the process (self-tests) | implemented | — |
| i8042 PS/2 keyboard | qemu-x86_64 | `drivers/pc/ps2kbd.c`, set 1 | PIC | QMP key events drive the shell, calculator and tasks (`make test`) | implemented | USB HID not done |
| PS/2 mouse / pointer | qemu-x86_64 | — | i8042 | — | unsupported | next input item (M2) |
| Touch | qemu-x86_64 / phone | — | virtio-input / vendor touch IC | — | unsupported | M3 / M6 |
| CMOS RTC | qemu-x86_64 | `drivers/pc/rtc_pci_power.c` | — | clock shows the time set with `-rtc base=` | implemented | no time zones |
| PCI enumeration | qemu-x86_64 | config mechanism #1, buses 0–7 | — | 6 devices found and listed (serial log, Settings) | implemented | no BAR or MSI handling yet |
| Framebuffer | qemu-x86_64 (BIOS + UEFI) | bootloader-provided linear FB (Multiboot2 tag 8) | GRUB | 1280×800×32, screenshots in `make test` | implemented | resolution is fixed by the bootloader |
| virtio-gpu | qemu-x86_64 | — | PCI, virtqueues | — | unsupported | M3 |
| virtio-blk | qemu-x86_64 | — | PCI, virtqueues | — | unsupported | **M2, next** |
| virtio-net | qemu-x86_64 | — | PCI, virtqueues | — | unsupported | M4 |
| Entropy (RDRAND / virtio-rng) | qemu-x86_64 | — | — | — | unsupported | M2 (needed before signing/TLS) |
| Audio (virtio-snd / HDA) | qemu-x86_64 | — | — | — | unsupported | after M4 |
| Power off / reboot | qemu-x86_64 | fixed QEMU PM1a port 0x604, i8042 reset | — | "Shut down" in Settings exits QEMU (`make test`) | emulated | real hardware needs an ACPI (AML) interpreter |
| Battery | any | — | ACPI / PMIC | — | unsupported | Settings says "Not supported yet" |
| PL011 UART | qemu-aarch64 | `drivers/virt/pl011.c`, polled TX | — | all serial markers in `make test ARCH=aarch64` | implemented | — |
| GICv2 | qemu-aarch64 | `drivers/virt/gic_timer.c` | — | timer and virtio IRQs delivered | emulated | GICv3 not supported yet |
| ARM generic timer (virtual, PPI 27) | qemu-aarch64 | `drivers/virt/gic_timer.c`, 250 Hz | GIC | preemption, sleep, uptime | implemented | — |
| Exceptions / MMU | qemu-aarch64 | `arch/aarch64/vectors.S`, `vmm.c`, 4 KiB granule | — | the same 14 protection self-tests pass (EL0 faults kill only the process) | implemented | no ASIDs, no PAN yet |
| fw_cfg + ramfb display | qemu-aarch64 | `drivers/virt/fw_cfg_ramfb.c` | fw_cfg DMA | 1280×800 screenshots identical to x86-64 | emulated | virtio-gpu not done |
| virtio-mmio transport (modern v2) + virtio-input keyboard | qemu-aarch64 | `drivers/virt/virtio_input.c` | GIC | QMP key events drive the shell (`make test ARCH=aarch64`) | implemented | legacy (v1) transport unsupported; needs `-global virtio-mmio.force-legacy=false`; no pointer device yet |
| PL031 RTC | qemu-aarch64 | `drivers/virt/rtc_psci.c` | — | clock shows `-rtc base=` time | implemented | — |
| PSCI power off / reset | qemu-aarch64 | HVC conduit | — | "Shut down" exits QEMU | emulated | conduit is fixed to HVC (no DTB parsing) |
| Phone: display, touch, UFS, USB, PMIC, charger | Nothing Phone (3a) Lite | — | see PHONE_PORT.md | — | blocked | no verified boot path or hardware docs |
| Phone: Wi-Fi, Bluetooth, LTE/5G modem | Nothing Phone (3a) Lite | — | vendor firmware, modem interface | — | blocked | see PHONE_PORT.md; virtio-net does **not** count as Wi-Fi or 4G |

Network protocols (Ethernet, ARP, IPv4, ICMP, UDP, TCP, DHCP, DNS): **none implemented yet** (M4).
IPv6 and TLS are tracked separately and are also not implemented.
