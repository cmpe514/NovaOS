# Dependency and ownership table

**Own** = written in this repository for NovaOS. **Reused** = an established external component, not
modified.

| Area | Component | Own / reused | Role | Notes |
|---|---|---|---|---|
| Kernel | `kernel/` (memory, tasks, syscalls, ELF loader, panic screen) | Own | Core kernel | |
| Architecture | `arch/x86_64/` (entry, long mode, GDT/TSS/IDT, ISR stubs, page tables) | Own | x86-64 support | |
| Architecture | `arch/aarch64/` (entry, exception vectors, MMU, page tables) | Own | ARM64 support | |
| Drivers | `drivers/virt/` (PL011, GICv2, generic timer, fw_cfg/ramfb, virtio-mmio input, PL031, PSCI) | Own | QEMU virt hardware | |
| Drivers | `drivers/pc/` (16550 UART, 8259 PIC, 8254 PIT, i8042 PS/2 keyboard, CMOS RTC, PCI scan, QEMU power) | Own | QEMU PC hardware | See DRIVERS.md |
| Userspace | `libs/libnova`, `services/init`, `shell/`, `recovery/`, `tests/kernel/probe.c` | Own | Runtime, init, shell, apps, recovery | |
| Graphics | `libs/gfx`, `libs/novaui` | Own | Software renderer and UI chrome | |
| Theme | `theme/tokens.json`, `theme/tools/gen-assets.mjs` | Own | Design tokens and asset generator | Values taken from the supplied `ui.html` |
| Bootloader | GRUB 2.12 | Reused | Loads the kernel (Multiboot2), BIOS and UEFI | GPL-3.0; shipped in the ISO |
| Fonts | DM Sans, Space Mono | Reused | Typography from the reference | OFL-1.1, embedded as bitmaps |
| Icons | Lucide 0.577.0 | Reused | Icons from the reference | ISC, embedded as bitmaps |
| Toolchain | clang/lld 18, GNU make, grub-mkrescue, xorriso, mtools | Reused | Build | Not shipped |
| Test | QEMU 8.2 (x86_64 + aarch64), OVMF, Python 3, Pillow | Reused | Emulation, screenshots | Not shipped |
| Asset build | Node.js, Playwright Chromium | Reused | Rasterises the theme at build time only | Not shipped, and not needed for a normal build |
| Firmware | none (QEMU targets) | — | — | Phone firmware: PHONE_PORT.md |

Nothing from Linux, Android or any other OS kernel is used. Vendor Linux sources may later be read as
**hardware documentation** for the phone port; no code will be copied from them (licence and design reasons).
