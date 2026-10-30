/* User program images embedded in the kernel (built by the Makefile into build/x86_64/user/). */
.section .rodata
.balign 16
.global prog_init, prog_init_end, prog_shell, prog_shell_end, prog_recovery, prog_recovery_end, prog_probe, prog_probe_end
prog_init: .incbin "init.elf"
prog_init_end:
.balign 16
prog_shell: .incbin "shell.elf"
prog_shell_end:
.balign 16
prog_recovery: .incbin "recovery.elf"
prog_recovery_end:
.balign 16
prog_probe: .incbin "probe.elf"
prog_probe_end:
