# NovaOS security model (kernel M1)

## Enforced and tested now

`make test` runs these checks on every boot; they appear as `NOVA_SELFTEST` lines in the serial log.

- **Privilege separation.** Applications run in ring 3. The kernel's mappings are supervisor-only, and
  user mode has no port I/O (no I/O bitmap, IOPL 0).
  - Self-tests: `user_read_kernel_memory_killed`, `user_privileged_instruction_killed`,
    `user_port_io_killed`.
- **Memory protection.**
  - Every process has its own address space.
  - Code is read-only, and data and stack are non-executable (NX, W^X enforced by the loader).
  - Self-tests: `user_write_own_code_killed`, `user_execute_stack_killed`.
- **Faults are contained.** A faulting process is killed and its pages are freed, and the kernel keeps
  running (`kernel_survives_and_reaps`).
- **Validated syscalls.**
  - Unknown numbers return `-ENOSYS`.
  - Pointers are checked page by page against the caller's page tables, including the writable bit.
  - Lengths are bounded.
  - Self-tests: `invalid_syscall_enosys`, `write_kernel_pointer_efault`, `write_unmapped_pointer_efault`,
    `write_oversized_einval`, `sysinfo_into_readonly_efault`.
- **Explicit device permissions.** The framebuffer, input, process creation and power control require
  capabilities. A process can never grant more than it holds.
  - Self-tests: `capability_fb_denied`, `capability_escalation_denied`.
- **Validated executables.** The ELF loader checks every header field and segment bound.

## Not done yet (do not rely on it)

- There is no persistent storage, so there is no crash-safe storage or power-loss testing yet (M2).
- There are no updates, no signatures, no A/B slots and no rollback (M4).
- There is no entropy source, no ASLR and no stack canaries in the kernel.
- There is no SMEP/SMAP yet. The kernel reads user memory directly after validating it.
- There is no IOMMU, and drivers run in the kernel.
- There is no secure boot. Trust currently begins at whatever loads GRUB: anyone with physical access
  can replace GRUB or the kernel. Package signing (planned) will authenticate updates, but it is
  **not** a hardware-enforced boot chain.
- The kernel trusts the bootloader's memory map and framebuffer description, after sanity checks.

## Reporting

Open a private security advisory on the GitHub repository.
