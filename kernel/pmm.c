/* Physical page allocator: one bit per 4 KiB page in [KERNEL_RAM_BASE, KERNEL_RAM_LIMIT), filled from the
 * memory regions the architecture code reports. */
#include "kernel.h"

#define NPAGES ((KERNEL_RAM_LIMIT - KERNEL_RAM_BASE) / PAGE)
static uint64_t bitmap[NPAGES / 64];   /* 1 = used */
static uint64_t total_pages, free_pages, hint;

static void set_used(uint64_t p, bool used) {
    if (used) bitmap[p / 64] |= 1ull << (p % 64); else bitmap[p / 64] &= ~(1ull << (p % 64));
}
static bool is_used(uint64_t p) { return bitmap[p / 64] >> (p % 64) & 1; }

static bool inited;
void pmm_add_region(uint64_t base, uint64_t len) {
    if (!inited) { memset(bitmap, 0xff, sizeof bitmap); inited = true; }
    for (uint64_t a = (base + PAGE - 1) & ~(PAGE - 1); a + PAGE <= base + len; a += PAGE) {
        if (a < KERNEL_RAM_BASE || a >= KERNEL_RAM_LIMIT) continue;
        uint64_t p = (a - KERNEL_RAM_BASE) / PAGE;
        if (is_used(p)) { set_used(p, false); free_pages++; total_pages++; }
    }
}

void pmm_reserve(uint64_t base, uint64_t len) {
    for (uint64_t a = base & ~(PAGE - 1); a < base + len; a += PAGE) {
        if (a < KERNEL_RAM_BASE || a >= KERNEL_RAM_LIMIT) continue;
        uint64_t p = (a - KERNEL_RAM_BASE) / PAGE;
        if (!is_used(p)) { set_used(p, true); free_pages--; total_pages--; }
    }
}

uint64_t pmm_alloc(void) {
    for (uint64_t n = 0; n < NPAGES; n++) {
        uint64_t p = (hint + n) % NPAGES;
        if (bitmap[p / 64] == ~0ull) { n += 63 - p % 64; continue; }
        if (!is_used(p)) {
            set_used(p, true); free_pages--; hint = p + 1;
            memset((void *)(KERNEL_RAM_BASE + p * PAGE), 0, PAGE);
            return KERNEL_RAM_BASE + p * PAGE;
        }
    }
    return 0;
}

void pmm_free(uint64_t pa) {
    uint64_t p = (pa - KERNEL_RAM_BASE) / PAGE;
    KASSERT(pa % PAGE == 0 && pa >= KERNEL_RAM_BASE && p < NPAGES && is_used(p));
    set_used(p, false); free_pages++;
}

uint64_t pmm_total(void) { return total_pages; }
uint64_t pmm_free_count(void) { return free_pages; }
