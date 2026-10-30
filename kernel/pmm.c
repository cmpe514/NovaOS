/* Physical page allocator: one bit per 4 KiB page below KERNEL_RAM_LIMIT, built from the Multiboot2 memory map. */
#include "kernel.h"

#define NPAGES (KERNEL_RAM_LIMIT / PAGE)
static uint64_t bitmap[NPAGES / 64];   /* 1 = used */
static uint64_t total_pages, free_pages, hint;
extern char _kernel_end[];

static void set_used(uint64_t p, bool used) {
    if (used) bitmap[p / 64] |= 1ull << (p % 64); else bitmap[p / 64] &= ~(1ull << (p % 64));
}
static bool is_used(uint64_t p) { return bitmap[p / 64] >> (p % 64) & 1; }

void pmm_init(uint64_t mbi) {
    memset(bitmap, 0xff, sizeof bitmap);
    uint32_t mbi_size = *(uint32_t *)mbi;
    uint64_t reserved_end = (uint64_t)_kernel_end;
    if (mbi + mbi_size > reserved_end) reserved_end = mbi + mbi_size;
    for (uint64_t t = mbi + 8; t < mbi + mbi_size; t += (*(uint32_t *)(t + 4) + 7) & ~7ull) {
        uint32_t type = *(uint32_t *)t;
        if (type == 0) break;
        if (type != 6) continue;   /* memory map */
        uint32_t esize = *(uint32_t *)(t + 8);
        for (uint64_t e = t + 16; e < t + *(uint32_t *)(t + 4); e += esize) {
            uint64_t base = *(uint64_t *)e, len = *(uint64_t *)(e + 8);
            if (*(uint32_t *)(e + 16) != 1) continue;   /* available RAM only */
            for (uint64_t a = (base + PAGE - 1) & ~(PAGE - 1); a + PAGE <= base + len && a < KERNEL_RAM_LIMIT; a += PAGE) {
                if (a < 0x100000 || (a < reserved_end + PAGE && a + PAGE > 0x100000) || (a >= mbi && a < mbi + mbi_size)) continue;
                if (is_used(a / PAGE)) { set_used(a / PAGE, false); free_pages++; total_pages++; }
            }
        }
    }
    if (!free_pages) panic("no usable memory in the boot memory map");
    kprintf("pmm: %lu KiB free\n", free_pages * 4);
}

uint64_t pmm_alloc(void) {
    for (uint64_t n = 0; n < NPAGES; n++) {
        uint64_t p = (hint + n) % NPAGES;
        if (bitmap[p / 64] == ~0ull) { n += 63 - p % 64; continue; }
        if (!is_used(p)) {
            set_used(p, true); free_pages--; hint = p + 1;
            memset((void *)(p * PAGE), 0, PAGE);
            return p * PAGE;
        }
    }
    return 0;
}

void pmm_free(uint64_t pa) {
    uint64_t p = pa / PAGE;
    KASSERT(pa % PAGE == 0 && p < NPAGES && is_used(p));
    set_used(p, false); free_pages++;
}

uint64_t pmm_total(void) { return total_pages; }
uint64_t pmm_free_count(void) { return free_pages; }
