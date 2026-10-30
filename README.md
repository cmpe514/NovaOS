# NovaOS

NovaOS is an operating system with its **own kernel**, its own drivers, a native graphical shell and an
independent recovery environment. Its design follows the supplied [`ui/reference/ui.html`](ui/reference/ui.html).
It does not use Linux, Android, Chromium or Electron.

![NovaOS home screen, rendered by the NovaOS shell in QEMU](docs/img/home.png)

## Status (honest, as of 0.3.0-m5)

| | State | Evidence |
|---|---|---|
| x86-64 QEMU (BIOS and UEFI) boots the NovaOS kernel into the native shell | **VERIFIED** | `make test`: 42 checks, including 14 kernel protection self-tests, UI driven by key events, golden screenshots |
| ARM64 QEMU `virt` boots the same kernel into the shell and recovery | **VERIFIED** | `make test ARCH=aarch64`: 38 checks; same self-tests; screenshots pixel-identical to the x86-64 goldens |
| Nova Recovery (separate program, same design) | **VERIFIED**: menu, boot normal system, restart/power off | `make test` recovery run |
| Apps: Calculator, Tasks, Notes, Clock, Calendar, Settings, All apps | **VERIFIED in memory only** | Not saved: no storage driver yet |
| Storage, networking, mouse/touch, audio, updates | **Not implemented** | [docs/ROADMAP.md](docs/ROADMAP.md) |
| Nothing Phone (3a) Lite | **BLOCKED**, does not run on the phone | [docs/PHONE_PORT.md](docs/PHONE_PORT.md) |

Apps that need missing hardware support (Weather, Music, Photos, Files, Browser) say so on screen. They
never show sample data as if it were real.

## Try it

```bash
tools/nova-deps.sh
```
```bash
make doctor
```
```bash
make run
```

Use the arrow keys or Tab to move, Enter to open, and Esc or the Windows key to go home.
More options are in [INSTALL.md](INSTALL.md).

| Command | What it does |
|---|---|
| `make kernel ARCH=x86_64` | Builds `build/x86_64/novaos.elf` (kernel with init, shell, recovery) |
| `make image TARGET=qemu-x86_64` | Builds the bootable hybrid ISO in `out/` plus a `.sha256` |
| `make run TARGET=qemu-x86_64` | Boots it in QEMU (BIOS); `make run-uefi` for UEFI |
| `make recovery TARGET=qemu-x86_64` | Boots straight into Nova Recovery |
| `make test` | Boots BIOS, UEFI and recovery images in QEMU and checks markers, input and screenshots |
| `make package` | `out/novaos-<ver>-qemu-x86_64.tar.gz` with images, kernel, manifest and checksums |
| `make image ARCH=aarch64` | Builds `out/novaos-<ver>-qemu-aarch64.elf` (QEMU `virt` loads it with `-kernel`) |
| `make run ARCH=aarch64` / `make recovery ARCH=aarch64` / `make test ARCH=aarch64` | Same as above, on the ARM64 QEMU `virt` machine |

**Serial markers.** The serial log shows `NOVA_KERNEL_READY`, `NOVA_USERSPACE_READY`, `NOVA_SELFTEST_DONE`,
`NOVA_SHELL_READY` and `NOVA_RECOVERY_READY`. The tests also compare screenshots, because a marker alone
does not prove the screen is right.

**Reproducibility.** The kernel ELF builds bit-for-bit identically from a clean tree (checked with the
same toolchain). The ISO does not yet, because GRUB/xorriso embed a build-time volume UUID. Compare
`novaos.elf` checksums in `MANIFEST.txt`.

## Repository

```
arch/x86_64/     boot entry, long mode, GDT/TSS/IDT, interrupt stubs, page tables
arch/aarch64/    EL2->EL1 entry, exception vectors, MMU (4 KiB granule), page tables
kernel/          memory, tasks, scheduler, syscalls, ELF loader, panic screen
drivers/pc/      x86 PC: serial, PIC/PIT, PS/2 keyboard, RTC, PCI, QEMU power
drivers/virt/    ARM64 QEMU virt: PL011, GICv2 + generic timer, fw_cfg + ramfb, virtio-mmio keyboard, PL031, PSCI
include/nova/    kernel <-> user ABI
libs/            libnova (user runtime), gfx (renderer), novaui (shared chrome)
services/init/   first process: self-tests, starts and supervises the shell/recovery
shell/           native NovaOS shell and apps
recovery/        Nova Recovery
theme/           design tokens from ui.html + generated fonts/icons/wallpaper
ui/reference/    the supplied ui.html, unchanged
boot/grub/       boot menu
tools/           doctor, deps, system test, packaging
tests/           protection probe, golden screenshots
docs/            ADR, ownership, drivers, security, recovery, phone port, roadmap
legacy.mk, docs/legacy/, daemon/, image/, ui/src/   the earlier Debian-based appliance (superseded)
```

Documentation: [ADR](docs/ADR.md) · [Ownership](docs/OWNERSHIP.md) · [Drivers](docs/DRIVERS.md) ·
[Security](docs/SECURITY.md) · [Recovery](docs/RECOVERY.md) · [Phone port](docs/PHONE_PORT.md) ·
[Roadmap](docs/ROADMAP.md) · [Third-party licences](THIRD_PARTY_LICENSES)

### Rendering limitations compared with ui.html

- **Blur.** Glass blur is a 3-pass box blur of the wallpaper behind the panel. That matches the
  reference's `backdrop-filter`, because only the wallpaper is ever behind a panel.
- **Icons and fonts.** These are pre-rendered bitmaps at the reference sizes, not scalable.
- **No animations or transitions yet.** That also satisfies reduced motion.
- **No pointer input yet.**
- **Fixed resolution.** The layout scales from the reference's 1440×900 to the framebuffer size,
  which defaults to 1280×800.
