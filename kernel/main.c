/* NovaOS kernel entry and interrupt dispatch. */
#include "kernel.h"

char boot_cmdline[128];
uint32_t boot_mode;
fb_t fb;

static const char *exc_names[32] = { "divide error", "debug", "NMI", "breakpoint", "overflow", "bound", "invalid opcode",
    "no FPU", "double fault", "", "invalid TSS", "segment not present", "stack fault", "general protection",
    "page fault", "", "x87", "alignment", "machine check", "SIMD" };

frame_t *isr_dispatch(frame_t *f) {
    if (f->vec < 32) {
        uint64_t cr2 = f->vec == 14 ? read_cr2() : 0;
        if ((f->cs & 3) == 3) {
            kprintf("USER_FAULT pid=%d name=%s vec=%u (%s) rip=%lx addr=%lx err=%lx\n", current->pid, current->name,
                    (unsigned)f->vec, exc_names[f->vec] ? exc_names[f->vec] : "?", f->rip, cr2, f->err);
            return task_exit(f, EXIT_FAULT | f->vec);
        }
        panic("kernel exception %u (%s) rip=%lx addr=%lx err=%lx", (unsigned)f->vec,
              exc_names[f->vec] ? exc_names[f->vec] : "?", f->rip, cr2, f->err);
    }
    if (f->vec >= 32 && f->vec < 48) {
        int irq = f->vec - 32;
        pic_eoi(irq);
        if (irq == 0) { ticks++; return (ticks % 5 == 0 || current->pid == 0) ? schedule(f) : f; }
        if (irq == 1) kbd_irq();
        return f;
    }
    if (f->vec == 0x80) {
        if (f->rax == SYS_EXIT) return task_exit(f, f->rdi & 0xff);
        bool resched = false;
        f->rax = syscall_dispatch(f, &resched);
        return resched ? schedule(f) : f;
    }
    panic("unexpected interrupt %u", (unsigned)f->vec);
}

static void parse_mbi(uint64_t mbi) {
    uint32_t size = *(uint32_t *)mbi;
    for (uint64_t t = mbi + 8; t < mbi + size; t += (*(uint32_t *)(t + 4) + 7) & ~7ull) {
        uint32_t type = *(uint32_t *)t;
        if (type == 0) break;
        if (type == 1) {
            const char *s = (const char *)(t + 8);
            for (int i = 0; i < 127 && s[i]; i++) boot_cmdline[i] = s[i];
        } else if (type == 8) {
            fb.phys = *(uint64_t *)(t + 8); fb.pitch = *(uint32_t *)(t + 16);
            fb.width = *(uint32_t *)(t + 20); fb.height = *(uint32_t *)(t + 24); fb.bpp = *(uint8_t *)(t + 28);
            fb.ok = *(uint8_t *)(t + 29) == 1 && fb.bpp == 32 && fb.phys >= 0x80000000ull && fb.phys + (uint64_t)fb.pitch * fb.height <= 0x100000000ull;
        }
    }
}

void kmain(uint32_t magic, uint64_t mbi) {
    serial_init();
    kprintf("\nNovaOS kernel %s (x86_64)\n", NOVA_VERSION);
    if (magic != 0x36d76289) panic("not started by a Multiboot2 loader (magic %x)", magic);
    parse_mbi(mbi);
    kprintf("cmdline: '%s'\n", boot_cmdline);
    kprintf("framebuffer: %ux%u bpp=%u pitch=%u at %lx %s\n", fb.width, fb.height, fb.bpp, fb.pitch, fb.phys,
            fb.ok ? "ok" : "UNUSABLE (outside the 2-4 GiB device window or not 32-bit RGB)");
    boot_mode = str_contains(boot_cmdline, "nova.recovery") ? BOOT_RECOVERY :
                str_contains(boot_cmdline, "nova.safe") ? BOOT_SAFE : BOOT_NORMAL;
    cpu_init();
    pmm_init(mbi);
    { extern uint64_t boot_pml4[]; kernel_pml4 = (uint64_t)boot_pml4; }
    pic_init();
    pit_init(TICK_HZ);
    kbd_ok = kbd_init();
    pci_scan();
    task_init();
    kprintf("NOVA_KERNEL_READY mem_free_kib=%lu fb=%s kbd=%s pci=%d mode=%u\n", pmm_free_count() * 4,
            fb.ok ? "yes" : "no", kbd_ok ? "yes" : "no", pci_count, boot_mode);
    int pid = task_spawn("init", CAP_ALL, boot_mode, 0);
    if (pid < 0) panic("could not start init (%d)", pid);
    __asm__ volatile("sti");
    for (;;) __asm__ volatile("hlt");
}
