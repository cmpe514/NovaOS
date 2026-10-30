# NovaOS data storage

## What exists (0.4)

- **Driver.** virtio-blk, on legacy virtio-PCI (x86-64) and modern virtio-mmio (ARM64 `virt`). It runs
  polled and synchronously, one request at a time, through a kernel bounce buffer (`kernel/virtio_blk.c`).
  It uses FLUSH when the device offers it.
- **Store.** A crash-safe A/B snapshot store (`kernel/store.c`). It is a record store, **not a file
  system**: there are no files or directories yet, so the Files and Photos apps stay unavailable.

| Sectors | Contents |
|---|---|
| 0 | label `NOVAOS-DATA-DISK` |
| 8–135 | slot A: header sector + up to 127 data sectors (62 KiB) |
| 136–263 | slot B |

The header holds the magic `NOVASNAP`, version, generation, length, a CRC-32 of the data and a CRC-32 of
the header.

- **Writing.** A write goes to the slot that does not hold the current snapshot, in this order: data,
  FLUSH, header, FLUSH. If power fails part-way, that slot's checksum fails, and the previous snapshot is
  still the newest valid one.
- **Booting.** At boot the store reads both slots and uses the valid one with the higher generation.
- **Disk safety.** A blank (all-zero) disk is labelled on first use. A disk whose first sector is neither
  blank nor labelled is **refused** ("Disk has unknown contents: not used"), so NovaOS never overwrites a
  disk it does not recognise.
- **Access.** The `CAP_STORAGE` capability is held by the shell and Nova Recovery only. Syscalls validate
  every pointer and length, and erasing requires an explicit magic argument.
- **What is saved.** The shell saves notes, tasks and the "24-hour clock" setting
  (`include/nova/shellstate.h`) after every change.

## Tested (`make test`, both architectures)

- Changes survive a full power-off and boot.
- **Interrupted-write test.** The test damages the newest slot's data, as a torn write would, then
  reboots. The store must reject that slot and load the previous generation.
- **Recovery.**
  - *Check and repair storage* rewrites the newest valid snapshot, so both slots are valid again.
  - *Reset settings* keeps notes and tasks.
  - *Factory reset* refuses a wrong confirmation and erases only after `ERASE` is typed.

## Not done

- Random power-cut testing (killing QEMU mid-write).
- A real file system.
- Backup to a second disk or USB.
- Encryption.
- Wear levelling.
- The phone's UFS storage.
