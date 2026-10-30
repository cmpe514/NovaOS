# Installing and running NovaOS

NovaOS currently runs **only in the QEMU emulator**, on x86-64 (PC) and ARM64 (`virt` machine). It does not run on real PCs (untested) or
phones (blocked, see docs/PHONE_PORT.md).

## Linux (Ubuntu 24.04 / Debian 13)

```bash
git clone https://github.com/cmpe514/NovaOS && cd NovaOS
```
```bash
tools/nova-deps.sh
```
```bash
make run
```

## Windows (through WSL2)

1. In PowerShell (as administrator), install Ubuntu, then restart Windows if asked:
   ```powershell
   wsl --install -d Ubuntu-24.04
   ```
2. Open **Ubuntu** from the Start menu and run the three Linux commands above **inside the Linux home
   folder** (`cd ~` first, not `/mnt/c/...`).
3. QEMU opens its window through WSLg (Windows 11, or Windows 10 with WSLg). If no window appears, run
   `make image` and open the ISO in QEMU for Windows instead:
   ```powershell
   qemu-system-x86_64 -m 512 -device VGA -cdrom novaos-0.4.0-qemu-x86_64.iso
   ```

## ARM64 (QEMU virt)

```bash
make run ARCH=aarch64
```

Or run a built kernel directly:

```bash
qemu-system-aarch64 -M virt -cpu cortex-a72 -m 512 -serial stdio -device ramfb -device virtio-keyboard-device -global virtio-mmio.force-legacy=false -kernel novaos-0.4.0-qemu-aarch64.elf
```

Add `-fw_cfg name=opt/nova/cmdline,string=nova.recovery` to start Nova Recovery.

## Using a downloaded image

Each CI run uploads `novaos-<version>-qemu-x86_64.iso` and its `.sha256` (Actions → ci → artifact
*novaos-qemu-x86_64*). Check the checksum first:

```bash
sha256sum -c novaos-0.4.0-qemu-x86_64.iso.sha256
```
```bash
qemu-system-x86_64 -m 512 -device VGA -serial stdio -cdrom novaos-0.4.0-qemu-x86_64.iso
```

For UEFI, add `-bios /usr/share/ovmf/OVMF.fd`. VirtualBox may work (VM type *Other/Unknown 64-bit*, the ISO
in the optical drive), but it is **untested**.

Your notes, tasks and settings are kept in `out/data-<arch>.img`. Delete that file to start fresh.

## Keys

| Key | Action |
|---|---|
| Arrows, Tab | Move focus |
| Enter | Open or confirm |
| Esc, Windows key, F1 | Back to the home screen |

To reach Nova Recovery, choose it in the boot menu (it shows for 3 seconds).
