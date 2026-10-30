/* x86-64 boot (Multiboot2 from GRUB) and interrupt dispatch. */
#include "kernel.h"

void cpu_init(void);
void cpu_set_kernel_stack(uint64_t top);
void serial_init(void);
void pic_init(void);
void pic_eoi(int irq);
void pit_init(uint32_t hz);
bool kbd_init(void);
void kbd_irq(void);
int pci_scan(void);

uint32_t elf_machine = 62;   /* EM_X86_64 */
const char arch_name[] = "x86-64";

static const char *exc_names[32] = { "divide error", "debug", "NMI", "breakpoint", "overflow", "bound", "invalid opcode",
    "no FPU", "double fault", "", "invalid TSS", "segment not present", "stack fault", "general protection",
    "page fault", "", "x87", "alignment", "machine check", "SIMD" };

frame_t *isr_dispatch(frame_t *f) {
    if (f->vec < 32) {
        uint64_t cr2 = f->vec == 14 ? read_cr2() : 0;
        const char *n = exc_names[f->vec] ? exc_names[f->vec] : "?";
        if ((f->cs & 3) == 3) return kernel_user_fault(f, f->vec == 14 ? FAULT_PAGE : f->vec == 13 ? FAULT_PRIV : (unsigned)f->vec, n, f->rip, cr2);
        panic("kernel exception %u (%s) rip=%lx addr=%lx err=%lx", (unsigned)f->vec, n, f->rip, cr2, f->err);
    }
    if (f->vec < 48) {
        int irq = f->vec - 32;
        pic_eoi(irq);
        if (irq == 0) return kernel_tick(f);
        if (irq == 1 || irq == 12) kbd_irq();   /* both i8042 ports share one reader */
        return f;
    }
    if (f->vec == 0x80) return kernel_syscall(f);
    panic("unexpected interrupt %u", (unsigned)f->vec);
}

void arch_user_frame(frame_t *f, uint64_t entry, uint64_t sp, uint64_t arg) {
    memset(f, 0, sizeof *f);
    f->rip = entry; f->cs = 0x23; f->rflags = 0x202; f->rsp = sp; f->ss = 0x1b; f->rdi = arg;
}

void arch_switch_to(uint64_t pml4, uint64_t kstack_top) {
    if (kstack_top) cpu_set_kernel_stack(kstack_top);
    write_cr3(pml4);
}

void arch_cpu_name(char *out, size_t n) {
    uint32_t r[13] = { 0 };
    for (int i = 0; i < 3; i++) cpuid(0x80000002 + i, &r[i * 4], &r[i * 4 + 1], &r[i * 4 + 2], &r[i * 4 + 3]);
    memcpy(out, r, n < 48 ? n : 48);
    out[n - 1] = 0;
}

static void parse_mbi(uint64_t mbi) {
    uint32_t size = *(uint32_t *)mbi;
    extern char _kernel_end[];
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
        } else if (type == 6) {
            uint32_t esize = *(uint32_t *)(t + 8);
            for (uint64_t e = t + 16; e < t + *(uint32_t *)(t + 4); e += esize)
                if (*(uint32_t *)(e + 16) == 1) pmm_add_region(*(uint64_t *)e, *(uint64_t *)(e + 8));
        }
    }
    pmm_reserve(0, 0x100000);
    pmm_reserve(0x100000, (uint64_t)_kernel_end - 0x100000);
    pmm_reserve(mbi, size);
}

void kmain(uint32_t magic, uint64_t mbi) {
    serial_init();
    kprintf("\nNovaOS kernel %s (x86_64)\n", NOVA_VERSION);
    if (magic != 0x36d76289) panic("not started by a Multiboot2 loader (magic %x)", magic);
    parse_mbi(mbi);
    cpu_init();
    { extern uint64_t boot_pml4[]; kernel_pml4 = (uint64_t)boot_pml4; }
    pic_init();
    pit_init(TICK_HZ);
    kbd_ok = kbd_init();
    device_count = pci_scan();
    kernel_start();
}
