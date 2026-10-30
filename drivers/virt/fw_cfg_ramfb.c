/* QEMU fw_cfg (MMIO at 0x09020000): RAM size, the NovaOS command line (opt/nova/cmdline), and the ramfb
 * display, configured by DMA-writing etc/ramfb. All fw_cfg registers and structures are big-endian. */
#include "kernel.h"
#define FWCFG 0x09020000ull

static void select_key(uint16_t key) { dsb(); *(volatile uint16_t *)(FWCFG + 8) = __builtin_bswap16(key); dsb(); }
static void read_bytes(void *buf, size_t n) { for (size_t i = 0; i < n; i++) ((uint8_t *)buf)[i] = *(volatile uint8_t *)FWCFG; }

static bool find_file(const char *name, uint16_t *sel, uint32_t *size) {
    select_key(0x19);
    uint32_t count; read_bytes(&count, 4); count = __builtin_bswap32(count);
    for (uint32_t i = 0; i < count && i < 256; i++) {
        struct { uint32_t size; uint16_t sel, res; char name[56]; } e;
        read_bytes(&e, sizeof e);
        if (!strcmp(e.name, name)) { *sel = __builtin_bswap16(e.sel); *size = __builtin_bswap32(e.size); return true; }
    }
    return false;
}

uint64_t fw_cfg_ram_size(void) {
    char sig[4];
    select_key(0); read_bytes(sig, 4);
    if (memcmp(sig, "QEMU", 4)) return 0;
    uint64_t v = 0; select_key(0x03); read_bytes(&v, 8);   /* numeric items are little-endian */
    return v;
}

void fw_cfg_cmdline(char *out, size_t n) {
    uint16_t sel; uint32_t size;
    if (!find_file("opt/nova/cmdline", &sel, &size)) return;
    select_key(sel);
    read_bytes(out, size < n - 1 ? size : n - 1);
}

static struct { uint32_t control, length; uint64_t address; } __attribute__((aligned(16))) dma;
static struct __attribute__((packed)) { uint64_t addr; uint32_t fourcc, flags, width, height, stride; } cfg;

bool ramfb_init(uint64_t phys, uint32_t w, uint32_t h) {
    uint16_t sel; uint32_t size;
    if (!find_file("etc/ramfb", &sel, &size) || size != sizeof cfg) { kprintf("ramfb: not present (add -device ramfb)\n"); return false; }
    cfg.addr = __builtin_bswap64(phys); cfg.fourcc = __builtin_bswap32(0x34325258);   /* XR24 = XRGB8888 */
    cfg.flags = 0; cfg.width = __builtin_bswap32(w); cfg.height = __builtin_bswap32(h); cfg.stride = __builtin_bswap32(w * 4);
    dma.control = __builtin_bswap32((uint32_t)sel << 16 | 0x08 | 0x10);              /* select + write */
    dma.length = __builtin_bswap32(sizeof cfg);
    dma.address = __builtin_bswap64((uint64_t)&cfg);
    dsb();
    *(volatile uint64_t *)(FWCFG + 16) = __builtin_bswap64((uint64_t)&dma);
    for (int i = 0; i < 1000000 && (__builtin_bswap32(*(volatile uint32_t *)&dma.control) & ~1u); i++) {}
    bool ok = dma.control == 0;
    kprintf("ramfb: %ux%u at %lx %s\n", w, h, phys, ok ? "ok" : "FAILED");
    return ok;
}
