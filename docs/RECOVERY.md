# Nova Recovery

## What exists today (M1, VERIFIED in QEMU by `make test`)

- **Separate program.** `recovery/main.c` is its own ELF; it shares no code with the shell except the
  theme libraries (`libs/gfx`, `libs/novaui`).
- **How it starts.** Choose **Nova Recovery** in the boot menu (kernel argument `nova.recovery`), or let
  `init` start it automatically after the shell fails three times in a row.
- **Design.** It uses the same theme as the shell: wallpaper, glass panels, accent colour, DM Sans,
  focus ring.
- **Navigation.** Up/Down, Enter, Esc.
- **Fallback.** With no usable display, it prints a text menu on the serial console.
- **Working actions.** Boot normal system (`init` then starts the shell), System information,
  Restart, Power off.
- **Everything else is shown as Unavailable, with the reason.** That covers safe mode, previous image,
  signed update, storage and logs, backup/restore, repair, reset settings and factory reset. They all
  depend on storage (M2) or updates (M4).
- **No user data needed.** Recovery reads nothing writable: there is no persistent storage yet at all.

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
