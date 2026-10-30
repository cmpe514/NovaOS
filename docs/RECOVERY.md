# Nova Recovery

## What exists today (0.4, VERIFIED in QEMU by `make test` on x86-64 and ARM64)

- **Separate program.** `recovery/main.c` is its own ELF; it shares no code with the shell except the
  theme libraries (`libs/gfx`, `libs/novaui`).
- **How it starts.** Choose **Nova Recovery** in the boot menu (kernel argument `nova.recovery`), or let
  `init` start it automatically after the shell fails three times in a row.
- **Design.** It uses the same theme as the shell: wallpaper, glass panels, accent colour, DM Sans,
  focus ring.
- **Navigation.** Up/Down, Enter, Esc.
- **Fallback.** With no usable display, it prints a text menu on the serial console.
- **Navigation.** Keyboard, mouse (x86-64) or tablet pointer (ARM64).
- **Working actions.** Boot normal system, System information, Restart, Power off, Storage information
  (disk, slot validity, generation), Check and repair storage, Reset settings (keeps notes and tasks),
  Factory reset.
- **Factory reset safeguard.** It names the disk and its size and requires typing `ERASE`. A wrong
  confirmation is refused, and the test checks this.
- **Still Unavailable, with the reason shown.** Safe mode, previous system image, install signed update,
  back up / restore (needs a second disk or USB).
- **No user data needed.** Recovery starts without the data disk. When the disk is missing or
  unreadable, it shows the reason instead.

## Planned design (not implemented)

- **Placement.** A disk layout of `[ESP/boot] [system A] [system B] [recovery] [data]`. Recovery has its
  own read-only image, so it does not depend on a system slot.
- **Trust.** Recovery verifies Ed25519 signatures on update packages before writing anything. Package
  signing is not secure boot: until a platform's firmware verifies the bootloader and kernel, someone with
  physical access can replace them (see SECURITY.md).
- **Interrupted updates.** The update is written to the inactive slot, which is marked bootable only after
  a full verify. The boot counter falls back to the old slot after 3 failed boots. Recovery stays bootable
  throughout, because it is never the slot being written.
- **Factory reset.** Names the device and the data partition and requires typing a confirmation word.
- **Phone.** Volume and power-button navigation, once those drivers exist. NovaOS does not assume the
  phone has a recovery partition or fastbootd.
