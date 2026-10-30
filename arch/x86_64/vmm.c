/* x86-64 address spaces. Every process gets its own PML4. The kernel's identity map of 0-1 GiB and 2-4 GiB is shared
 * as supervisor-only (no U bit), so ring 3 cannot touch it; 1-2 GiB is the private user region. */
#include "kernel.h"

#define P_PRESENT 1ull
#define P_WRITE 2ull
#define P_USER 4ull
#define P_DEVICE (1ull << 9)     /* software bit: mapped device memory, not owned by the process */
#define P_NX (1ull << 63)
#define ADDR(e) ((e) & 0x000ffffffffff000ull)

extern uint64_t boot_pml4[512], boot_pdpt[512];
uint64_t kernel_pml4;

uint64_t as_create(void) {
    uint64_t pml4 = pmm_alloc(), pdpt = pmm_alloc(), pd = pmm_alloc();
    if (!pml4 || !pdpt || !pd) {
        if (pml4) pmm_free(pml4);
        if (pdpt) pmm_free(pdpt);
        if (pd) pmm_free(pd);
        return 0;
    }
    uint64_t *l4 = (uint64_t *)pml4, *l3 = (uint64_t *)pdpt;
    l4[0] = pdpt | P_PRESENT | P_WRITE | P_USER;
    l3[0] = boot_pdpt[0];             /* supervisor-only kernel map */
    l3[1] = pd | P_PRESENT | P_WRITE | P_USER;
    l3[2] = boot_pdpt[2];
    l3[3] = boot_pdpt[3];
    return pml4;
}

static uint64_t *user_pd(uint64_t pml4) { return (uint64_t *)ADDR(((uint64_t *)ADDR(((uint64_t *)pml4)[0]))[1]); }

void as_destroy(uint64_t pml4) {
    uint64_t *pd = user_pd(pml4);
    for (int i = 0; i < 512; i++) {
        if (!(pd[i] & P_PRESENT)) continue;
        uint64_t *pt = (uint64_t *)ADDR(pd[i]);
        for (int j = 0; j < 512; j++)
            if ((pt[j] & P_PRESENT) && !(pt[j] & P_DEVICE)) pmm_free(ADDR(pt[j]));
        pmm_free((uint64_t)pt);
    }
    pmm_free((uint64_t)pd);
    pmm_free(ADDR(((uint64_t *)pml4)[0]));
    pmm_free(pml4);
}

bool as_map(uint64_t pml4, uint64_t va, uint64_t pa, bool writable, bool exec, bool device) {
    if (va < USER_BASE || va >= USER_TOP || va % PAGE || pa % PAGE) return false;
    uint64_t *pd = user_pd(pml4);
    int i = (va >> 21) & 511, j = (va >> 12) & 511;
    if (!(pd[i] & P_PRESENT)) {
        uint64_t pt = pmm_alloc();
        if (!pt) return false;
        pd[i] = pt | P_PRESENT | P_WRITE | P_USER;
    }
    uint64_t *pt = (uint64_t *)ADDR(pd[i]);
    if (pt[j] & P_PRESENT) return false;
    pt[j] = pa | P_PRESENT | P_USER | (writable ? P_WRITE : 0) | (exec ? 0 : P_NX) | (device ? P_DEVICE : 0);
    return true;
}

/* Is [va, va+len) entirely mapped user memory (and writable if `write`) in this address space? */
bool user_range_ok(uint64_t pml4, uint64_t va, uint64_t len, bool write) {
    if (va < USER_BASE || len > USER_TOP - USER_BASE || va > USER_TOP - len) return false;
    uint64_t *pd = user_pd(pml4);
    for (uint64_t p = va & ~(PAGE - 1); p < va + len; p += PAGE) {
        uint64_t pde = pd[(p >> 21) & 511];
        if (!(pde & P_PRESENT)) return false;
        uint64_t pte = ((uint64_t *)ADDR(pde))[(p >> 12) & 511];
        if (!(pte & P_PRESENT) || !(pte & P_USER) || (write && !(pte & P_WRITE))) return false;
    }
    return true;
}
