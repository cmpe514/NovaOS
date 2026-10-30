/* probe: deliberately misbehaving user program used by init's self-tests. Each mode must be stopped by the
 * kernel (exit code EXIT_FAULT | vector) or refused with an error. */
#include "nova.h"

static void target(void) {}

int main(uint64_t mode) {
    switch (mode) {
    case 0: return 42;                                                  /* normal exit */
    case 1: return *(volatile uint32_t *)0x100000;                      /* read kernel memory */
    case 2: *(volatile uint8_t *)(uintptr_t)&target = 0xc3; return 1;   /* write own (read-only) code */
    case 3: __asm__ volatile("cli"); return 1;                          /* privileged instruction */
    case 4: __asm__ volatile("outb %%al, $0x80" :: "a"(0)); return 1;   /* port I/O */
    case 5: { nova_fb_info i; return sys_fb_map(&i) == -E_PERM ? 0 : 1; }
    case 6: return sys_spawn("probe", CAP_ALL, 0) == -E_PERM ? 0 : 1;  /* ask for more than we hold */
    case 7: { volatile uint8_t code[4]; code[0] = 0xc3; ((void (*)(void))(uintptr_t)code)(); return 1; }  /* NX stack */
    }
    return 2;
}
