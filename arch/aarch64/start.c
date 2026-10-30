/* AArch64 boot for the QEMU virt machine and exception dispatch. */
#include "kernel.h"

void mmu_init(void);
void gic_init(void);
void timer_init(uint32_t hz);
frame_t *gic_irq(frame_t *f);
uint64_t fw_cfg_ram_size(void);
void fw_cfg_cmdline(char *out, size_t n);
bool ramfb_init(uint64_t phys, uint32_t w, uint32_t h);
int virtio_scan(void);
extern char vectors[], _kernel_end[];

uint32_t elf_machine = 183;   /* EM_AARCH64 */
const char arch_name[] = "aarch64";

frame_t *arm_exception(frame_t *f, uint64_t kind) {
    uint64_t esr, far;
    __asm__ volatile("mrs %0, esr_el1\n mrs %1, far_el1" : "=r"(esr), "=r"(far));
    unsigned ec = esr >> 26;
    if (kind == 5 || kind == 9) return gic_irq(f);                 /* IRQ from EL1 or EL0 */
    if (kind == 8) {                                               /* synchronous, from EL0 */
        if (ec == 0x15) return kernel_syscall(f);                  /* SVC */
        if (ec == 0x20 || ec == 0x24) return kernel_user_fault(f, FAULT_PAGE, ec == 0x20 ? "instruction abort" : "data abort", f->elr, far);
        return kernel_user_fault(f, FAULT_PRIV, ec == 0x18 ? "system register access" : ec == 0 ? "undefined instruction" : "exception", f->elr, esr);
    }
    panic("kernel exception kind=%lu esr=%lx (class %x) elr=%lx far=%lx", kind, esr, ec, f->elr, far);
}

void arch_user_frame(frame_t *f, uint64_t entry, uint64_t sp, uint64_t arg) {
    memset(f, 0, sizeof *f);
    f->x[0] = arg; f->elr = entry; f->sp_el0 = sp; f->spsr = 0;   /* EL0t, interrupts enabled */
}

void arch_switch_to(uint64_t l0, uint64_t kstack_top) {
    (void)kstack_top;   /* SP_EL1 is restored from the frame; nothing like a TSS is needed */
    __asm__ volatile("msr ttbr0_el1, %0\n isb\n dsb ishst\n tlbi vmalle1\n ic iallu\n dsb ish\n isb" :: "r"(l0) : "memory");
}

void arch_cpu_name(char *out, size_t n) {
    uint64_t midr;
    __asm__ volatile("mrs %0, midr_el1" : "=r"(midr));
    const char *s = ((midr >> 24) & 0xff) == 0x41 && ((midr >> 4) & 0xfff) == 0xd08 ? "ARM Cortex-A72" :
                    ((midr >> 24) & 0xff) == 0x41 && ((midr >> 4) & 0xfff) == 0xd03 ? "ARM Cortex-A53" : "AArch64 processor";
    size_t i = 0;
    for (; s[i] && i < n - 1; i++) out[i] = s[i];
    out[i] = 0;
}

void kmain(void) {
    mmu_init();
    __asm__ volatile("msr vbar_el1, %0\n isb" :: "r"(vectors));
    kprintf("\nNovaOS kernel %s (aarch64, QEMU virt)\n", NOVA_VERSION);
    uint64_t ram = fw_cfg_ram_size();
    if (!ram) ram = 512ull << 20;
    if (ram > KERNEL_RAM_LIMIT - KERNEL_RAM_BASE) ram = KERNEL_RAM_LIMIT - KERNEL_RAM_BASE;
    pmm_add_region(KERNEL_RAM_BASE, ram);
    pmm_reserve(KERNEL_RAM_BASE, (uint64_t)_kernel_end - KERNEL_RAM_BASE);   /* DTB + kernel */
    uint64_t fbsize = 1280ull * 800 * 4, fbphys = (KERNEL_RAM_BASE + ram - fbsize) & ~(PAGE - 1);
    pmm_reserve(fbphys, fbsize + PAGE);
    if (ramfb_init(fbphys, 1280, 800)) fb = (fb_t){ fbphys, 1280, 800, 1280 * 4, 32, true };
    fw_cfg_cmdline(boot_cmdline, sizeof boot_cmdline);
    gic_init();
    device_count = virtio_scan();
    timer_init(TICK_HZ);
    kernel_start();
}
