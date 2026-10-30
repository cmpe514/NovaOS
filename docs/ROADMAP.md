# NovaOS milestones

QEMU completion and phone completion are reported separately. **VERIFIED** = shown by `make test` in
QEMU; **UNVERIFIED** = code exists but no test shows it; **BLOCKED** = cannot proceed (reason given).

| Milestone | State | Evidence / what is missing |
|---|---|---|
| M1 Kernel boots in QEMU with serial and framebuffer | **VERIFIED** (x86-64, BIOS and UEFI) | `NOVA_KERNEL_READY`, screenshots in `out/test/` |
| M2 Userspace, storage, input, persistent settings | **Partly done.** Userspace, syscalls and keyboard: VERIFIED. Storage and persistence: not started | next items: virtio-blk, NovaFS, settings store, PS/2 mouse, power-loss test |
| M3 Native shell matches the UI, core apps work | **Partly done.** Home screen, Calculator, Tasks, Notes, Clock, Calendar, Settings and All apps work in memory. The home screen is VERIFIED against a golden screenshot | not saved to disk; no pointer/touch; no animations; Weather/Music/Photos/Files/Browser show "Not available" with reasons |
| M4 Networking and themed recovery | Recovery: **partly done** (VERIFIED menu, boot-normal, power actions). Networking: not started | virtio-net, TCP/IP, DHCP, DNS; signed updates; A/B slots |
| M5 ARM64 QEMU reaches shell and recovery | **VERIFIED** (QEMU `virt`, Cortex-A72) | `make test ARCH=aarch64`: boot, 14 self-tests, shell UI, recovery, PSCI power off. Not verified: GICv3, other CPUs, more than 1 GiB of RAM, SMP |
| M6 Verified phone boot | **BLOCKED** | docs/PHONE_PORT.md |
| M7 Phone install, restore, update, recovery | **BLOCKED** | depends on M6 and a verified stock restoration route |

The Browser needs networking, TLS and a ported web engine (for example NetSurf's libcss/libdom stack, or
a WebKit port). That is a multi-milestone project. An HTTP viewer would not be called a browser.
