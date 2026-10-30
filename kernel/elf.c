/* ELF64 loader for user programs (static, ET_EXEC, linked inside the user region). Validates every header
 * field it uses before touching memory. */
#include "kernel.h"

typedef struct { uint8_t ident[16]; uint16_t type, machine; uint32_t version; uint64_t entry, phoff, shoff;
                 uint32_t flags; uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx; } ehdr_t;
typedef struct { uint32_t type, flags; uint64_t offset, vaddr, paddr, filesz, memsz, align; } phdr_t;

uint64_t elf_load(uint64_t pml4, const uint8_t *img, uint64_t size) {
    if (size < sizeof(ehdr_t)) return 0;
    const ehdr_t *eh = (const ehdr_t *)img;
    if (memcmp(eh->ident, "\x7f" "ELF", 4) || eh->ident[4] != 2 || eh->ident[5] != 1 || eh->type != 2 ||
        eh->machine != 62 || eh->phentsize != sizeof(phdr_t) || eh->phnum > 16 ||
        eh->phoff > size || eh->phnum * sizeof(phdr_t) > size - eh->phoff)
        return 0;
    if (eh->entry < USER_BASE || eh->entry >= USER_FB) return 0;
    for (int i = 0; i < eh->phnum; i++) {
        const phdr_t *ph = (const phdr_t *)(img + eh->phoff + i * sizeof(phdr_t));
        if (ph->type != 1) continue;   /* PT_LOAD */
        if (ph->filesz > ph->memsz || ph->offset > size || ph->filesz > size - ph->offset ||
            ph->vaddr < USER_BASE || ph->memsz > USER_FB - USER_BASE || ph->vaddr > USER_FB - ph->memsz)
            return 0;
        bool w = ph->flags & 2, x = ph->flags & 1;
        if (w && x) return 0;          /* W^X */
        for (uint64_t va = ph->vaddr & ~(PAGE - 1); va < ph->vaddr + ph->memsz; va += PAGE) {
            uint64_t pa = pmm_alloc();
            if (!pa || !as_map(pml4, va, pa, w, x, false)) { if (pa) pmm_free(pa); return 0; }
            /* copy the part of the file image that falls into this page (pages are zeroed) */
            uint64_t lo = va > ph->vaddr ? va : ph->vaddr, hi = va + PAGE < ph->vaddr + ph->filesz ? va + PAGE : ph->vaddr + ph->filesz;
            if (lo < hi) memcpy((uint8_t *)pa + (lo - va), img + ph->offset + (lo - ph->vaddr), hi - lo);
        }
    }
    return eh->entry;
}
