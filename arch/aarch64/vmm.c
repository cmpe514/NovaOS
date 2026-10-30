/* AArch64 address spaces (4 KiB granule, 48-bit VA, TTBR0 only). L0[0] is the shared kernel identity map of
 * 0-2 GiB (EL1-only blocks: devices, then RAM); L0[1] is the per-process 1 GiB user region at USER_BASE. */
#include "kernel.h"

#define D_VALID   1ull
#define D_TABLE   3ull                  /* table (L0-L2) or page (L3) descriptor */
#define D_AF      (1ull << 10)
#define D_SH      (3ull << 8)           /* inner shareable */
#define D_NORMAL  (1ull << 2)           /* MAIR index 1 */
#define D_DEVICE  (0ull << 2)           /* MAIR index 0 */
#define D_EL0     (1ull << 6)           /* AP[1]: EL0 access */
#define D_RO      (1ull << 7)           /* AP[2]: read-only */
#define D_PXN     (1ull << 53)
#define D_UXN     (1ull << 54)
#define D_SWDEV   (1ull << 55)          /* software: device memory, not owned by the process */
#define ADDR(e)   ((e) & 0x0000fffffffff000ull)

static uint64_t kl0[512] __attribute__((aligned(4096))), kl1[512] __attribute__((aligned(4096)));
uint64_t kernel_pml4;

void mmu_init(void) {
    kl1[0] = 0x00000000ull | D_VALID | D_AF | D_DEVICE | D_PXN | D_UXN;            /* 0-1 GiB devices */
    kl1[1] = 0x40000000ull | D_VALID | D_AF | D_SH | D_NORMAL | D_UXN;             /* 1-2 GiB RAM */
    kl0[0] = (uint64_t)kl1 | D_TABLE;
    kernel_pml4 = (uint64_t)kl0;
    uint64_t mair = 0x00 | (0xffull << 8);                                         /* device nGnRnE, normal WB */
    uint64_t tcr = 16 | (1ull << 8) | (1ull << 10) | (3ull << 12) | (1ull << 23);   /* T0SZ=16, WB, inner sh., EPD1 */
    __asm__ volatile("msr mair_el1, %0\n msr tcr_el1, %1\n msr ttbr0_el1, %2\n isb\n"
                     "tlbi vmalle1\n dsb ish\n isb" :: "r"(mair), "r"(tcr), "r"(kernel_pml4) : "memory");
    uint64_t sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= 1 | (1 << 2) | (1 << 12);               /* MMU, D-cache, I-cache */
    sctlr &= ~((1ull << 1) | (1ull << 19) | (1ull << 9));   /* no alignment checks, no WXN, EL0 cannot mask IRQs */
    __asm__ volatile("msr sctlr_el1, %0\n isb" :: "r"(sctlr) : "memory");
}

uint64_t as_create(void) {
    uint64_t l0 = pmm_alloc(), l1 = pmm_alloc();
    if (!l0 || !l1) { if (l0) pmm_free(l0); if (l1) pmm_free(l1); return 0; }
    ((uint64_t *)l0)[0] = (uint64_t)kl1 | D_TABLE;
    ((uint64_t *)l0)[1] = l1 | D_TABLE;
    return l0;
}

static uint64_t *user_l1(uint64_t l0) { return (uint64_t *)ADDR(((uint64_t *)l0)[1]); }

void as_destroy(uint64_t l0) {
    uint64_t *l1 = user_l1(l0);
    for (int i = 0; i < 512; i++) {
        if (!(l1[i] & D_VALID)) continue;
        uint64_t *l2 = (uint64_t *)ADDR(l1[i]);
        for (int j = 0; j < 512; j++) {
            if (!(l2[j] & D_VALID)) continue;
            uint64_t *l3 = (uint64_t *)ADDR(l2[j]);
            for (int k = 0; k < 512; k++)
                if ((l3[k] & D_VALID) && !(l3[k] & D_SWDEV)) pmm_free(ADDR(l3[k]));
            pmm_free((uint64_t)l3);
        }
        pmm_free((uint64_t)l2);
    }
    pmm_free((uint64_t)l1);
    pmm_free(l0);
}

static uint64_t *next(uint64_t *t, int i) {
    if (!(t[i] & D_VALID)) { uint64_t p = pmm_alloc(); if (!p) return 0; t[i] = p | D_TABLE; }
    return (uint64_t *)ADDR(t[i]);
}

bool as_map(uint64_t l0, uint64_t va, uint64_t pa, bool writable, bool exec, bool device) {
    if (va < USER_BASE || va >= USER_TOP || va % PAGE || pa % PAGE) return false;
    uint64_t *l2 = next(user_l1(l0), (va >> 30) & 511), *l3 = l2 ? next(l2, (va >> 21) & 511) : 0;
    if (!l3) return false;
    int i = (va >> 12) & 511;
    if (l3[i] & D_VALID) return false;
    l3[i] = pa | D_TABLE | D_AF | D_SH | D_NORMAL | D_EL0 | D_PXN | (writable ? 0 : D_RO) | (exec ? 0 : D_UXN) | (device ? D_SWDEV : 0);
    return true;
}

bool user_range_ok(uint64_t l0, uint64_t va, uint64_t len, bool write) {
    if (va < USER_BASE || len > USER_TOP - USER_BASE || va > USER_TOP - len) return false;
    uint64_t *l1 = user_l1(l0);
    for (uint64_t p = va & ~(PAGE - 1); p < va + len; p += PAGE) {
        uint64_t e1 = l1[(p >> 30) & 511];
        if (!(e1 & D_VALID)) return false;
        uint64_t e2 = ((uint64_t *)ADDR(e1))[(p >> 21) & 511];
        if (!(e2 & D_VALID)) return false;
        uint64_t e3 = ((uint64_t *)ADDR(e2))[(p >> 12) & 511];
        if (!(e3 & D_VALID) || !(e3 & D_EL0) || (write && (e3 & D_RO))) return false;
    }
    return true;
}
