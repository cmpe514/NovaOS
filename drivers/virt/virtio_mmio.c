/* virtio-mmio (modern, version 2) transport for QEMU virt: 32 slots at 0x0a000000 (stride 0x200), interrupts
 * SPI 16+n (INTID 48+n). Drivers: virtio-input (keyboard, tablet: interrupt driven) and virtio-blk (polled). */
#include "kernel.h"
#include "virtio.h"
#define VIRTIO_BASE 0x0a000000ull
#define QSIZE 64
void gic_enable(int id);

struct vevent { uint16_t type, code; uint32_t value; };
typedef struct {
    uint64_t base; int irq; virtq_t q;
    struct vevent ev[QSIZE];
    uint8_t mem[2 * 4096] __attribute__((aligned(4096)));
    int abs_x, abs_y, btn; bool ptr_dirty;
} vinput_t;
static vinput_t inputs[2] __attribute__((aligned(4096)));
static int ninputs;
static uint8_t blk_mem[2 * 4096] __attribute__((aligned(4096)));
static virtq_t blk_q;
static uint64_t blk_base;

static void w64(uint64_t b, int off, uint64_t v) { mmio_w32(b + off, (uint32_t)v); mmio_w32(b + off + 4, (uint32_t)(v >> 32)); }

/* Negotiates VERSION_1 (+ `want` in the low feature word) and sets up queue 0 in `mem`. */
static bool setup(uint64_t b, uint32_t want, uint32_t *got, virtq_t *q, uint8_t *mem, uint16_t size) {
    mmio_w32(b + 0x70, 0);
    mmio_w32(b + 0x70, 1 | 2);
    mmio_w32(b + 0x14, 1);
    if (!(mmio_r32(b + 0x10) & 1)) return false;
    mmio_w32(b + 0x14, 0);
    *got = mmio_r32(b + 0x10) & want;
    mmio_w32(b + 0x24, 1); mmio_w32(b + 0x20, 1);
    mmio_w32(b + 0x24, 0); mmio_w32(b + 0x20, *got);
    mmio_w32(b + 0x70, 1 | 2 | 8);
    if (!(mmio_r32(b + 0x70) & 8)) return false;
    mmio_w32(b + 0x30, 0);
    if (mmio_r32(b + 0x34) < size) return false;
    *q = (virtq_t){ (struct vdesc *)mem, (struct vavail *)(mem + 2048), (struct vused *)(mem + 4096), size, 0, 0 };
    mmio_w32(b + 0x38, size);
    w64(b, 0x80, (uint64_t)q->desc); w64(b, 0x90, (uint64_t)q->avail); w64(b, 0xa0, (uint64_t)q->used);
    mmio_w32(b + 0x44, 1);
    return true;
}

static void blk_notify(void) { dsb(); mmio_w32(blk_base + 0x50, 0); }

int virtio_scan(void) {
    int n = 0;
    for (int i = 0; i < 32; i++) {
        uint64_t b = VIRTIO_BASE + i * 0x200;
        if (mmio_r32(b) != 0x74726976 || mmio_r32(b + 8) == 0) continue;
        uint32_t ver = mmio_r32(b + 4), id = mmio_r32(b + 8), got;
        kprintf("virtio-mmio slot %d: device %u (transport v%u)\n", i, id, ver);
        n++;
        if (ver != 2) { kprintf("virtio: legacy transport not supported (use -global virtio-mmio.force-legacy=false)\n"); continue; }
        if (id == 18 && ninputs < 2) {
            vinput_t *d = &inputs[ninputs];
            if (!setup(b, 0, &got, &d->q, d->mem, QSIZE)) continue;
            for (int k = 0; k < QSIZE; k++) {
                d->q.desc[k] = (struct vdesc){ (uint64_t)&d->ev[k], sizeof(struct vevent), VD_WRITE, 0 };
                d->q.avail->ring[k] = (uint16_t)k;
            }
            d->q.avail->idx = QSIZE;
            mmio_w32(b + 0x70, 1 | 2 | 8 | 4);
            dsb();
            mmio_w32(b + 0x50, 0);
            d->base = b; d->irq = 48 + i; d->abs_x = d->abs_y = 16384;
            gic_enable(d->irq);
            ninputs++; kbd_ok = true;
            kprintf("virtio-input: device %d ready (irq %d)\n", ninputs, d->irq);
        } else if (id == 2 && !blk_base) {
            if (!setup(b, 1u << 9, &got, &blk_q, blk_mem, 16)) continue;
            mmio_w32(b + 0x70, 1 | 2 | 8 | 4);
            blk_base = b; blk_q.notify = blk_notify;
            uint64_t sectors = mmio_r32(b + 0x100) | (uint64_t)mmio_r32(b + 0x104) << 32;
            virtio_blk_attach(&blk_q, sectors, got & (1u << 9));
        }
    }
    return n;
}

void virtio_irq(int intid) {
    for (int i = 0; i < ninputs; i++) {
        vinput_t *d = &inputs[i];
        if (d->irq != intid) continue;
        mmio_w32(d->base + 0x64, mmio_r32(d->base + 0x60));
        dsb();
        while (d->q.last_used != d->q.used->idx) {
            uint32_t id = d->q.used->ring[d->q.last_used % QSIZE].id;
            if (id < QSIZE) {
                struct vevent e = d->ev[id];
                if (e.type == 1 && e.code == 0x110) { d->btn = e.value != 0; d->ptr_dirty = true; }   /* BTN_LEFT */
                else if (e.type == 1 && e.code < 0x100) input_key(e.code, e.value != 0);           /* EV_KEY */
                else if (e.type == 3 && e.code == 0) { d->abs_x = e.value; d->ptr_dirty = true; }  /* ABS_X */
                else if (e.type == 3 && e.code == 1) { d->abs_y = e.value; d->ptr_dirty = true; }  /* ABS_Y */
                else if (e.type == 0 && d->ptr_dirty) { input_pointer_abs(d->abs_x, d->abs_y, d->btn); d->ptr_dirty = false; }
                d->q.avail->ring[d->q.avail->idx % QSIZE] = (uint16_t)id;
                dsb();
                d->q.avail->idx++;
            }
            d->q.last_used++;
        }
        dsb();
        mmio_w32(d->base + 0x50, 0);
    }
}
