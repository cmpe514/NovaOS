/* Portable kernel start-up and the entry points the architecture code calls. */
#include "kernel.h"

char boot_cmdline[128];
uint32_t boot_mode;
fb_t fb;
int device_count;
bool kbd_ok;
volatile uint64_t ticks;

frame_t *kernel_tick(frame_t *f) {
    ticks++;
    return (ticks % 5 == 0 || current->pid == 0) ? schedule(f) : f;   /* 20 ms time slice */
}

frame_t *kernel_syscall(frame_t *f) {
    if (SYSCALL_NR(f) == SYS_EXIT) return task_exit(f, SYSCALL_ARG0(f) & 0xff);
    bool resched = false, restart = false;
    uint64_t r = syscall_dispatch(f, &resched, &restart);
    if (restart) SYSCALL_RESTART(f); else SYSCALL_RET(f) = r;
    return resched ? schedule(f) : f;
}

frame_t *kernel_user_fault(frame_t *f, unsigned kind, const char *what, uint64_t pc, uint64_t addr) {
    kprintf("USER_FAULT pid=%d name=%s kind=%u (%s) pc=%lx addr=%lx\n", current->pid, current->name, kind, what, pc, addr);
    return task_exit(f, EXIT_FAULT | kind);
}

void kernel_start(void) {
    kprintf("cmdline: '%s'\n", boot_cmdline);
    kprintf("framebuffer: %ux%u bpp=%u pitch=%u at %lx %s\n", fb.width, fb.height, fb.bpp, fb.pitch, fb.phys, fb.ok ? "ok" : "UNUSABLE");
    boot_mode = str_contains(boot_cmdline, "nova.recovery") ? BOOT_RECOVERY :
                str_contains(boot_cmdline, "nova.safe") ? BOOT_SAFE : BOOT_NORMAL;
    kprintf("pmm: %lu KiB free\n", pmm_free_count() * 4);
    task_init();
    kprintf("NOVA_KERNEL_READY arch=%s mem_free_kib=%lu fb=%s kbd=%s devices=%d mode=%u\n", arch_name, pmm_free_count() * 4,
            fb.ok ? "yes" : "no", kbd_ok ? "yes" : "no", device_count, boot_mode);
    int pid = task_spawn("init", CAP_ALL, boot_mode, 0);
    if (pid < 0) panic("could not start init (%d)", pid);
    irq_enable();
    for (;;) cpu_wait();
}
