/* virtio-mmio (modern, version 2) transport and the virtio-input keyboard driver for QEMU virt.
 * 32 transport slots at 0x0a000000 (stride 0x200), interrupts SPI 16+n (INTID 48+n). */
#include "kernel.h"
#define VIRTIO_BASE 0x0a000000ull
#define QSIZE 64
void gic_enable(int id);

struct vdesc { uint64_t addr; uint32_t len; uint16_t flags, next; };
struct vavail { uint16_t flags, idx, ring[QSIZE]; };
struct vused { uint16_t flags, idx; struct { uint32_t id, len; } ring[QSIZE]; };
struct vevent { uint16_t type, code; uint32_t value; };

static uint64_t kbd_base;
static int kbd_irq_id = -1;
static struct vdesc *desc;
static struct vavail *avail;
static struct vused *used;
static struct vevent *events;
static uint16_t last_used;

static bool input_init(uint64_t b) {
    mmio_w32(b + 0x70, 0);
    mmio_w32(b + 0x70, 1 | 2);                                   /* ACKNOWLEDGE | DRIVER */
    mmio_w32(b + 0x14, 1);
    if (!(mmio_r32(b + 0x10) & 1)) return false;                 /* needs VIRTIO_F_VERSION_1 */
    mmio_w32(b + 0x24, 1); mmio_w32(b + 0x20, 1);
    mmio_w32(b + 0x24, 0); mmio_w32(b + 0x20, 0);
    mmio_w32(b + 0x70, 1 | 2 | 8);                               /* FEATURES_OK */
    if (!(mmio_r32(b + 0x70) & 8)) return false;
    mmio_w32(b + 0x30, 0);                                       /* event queue */
    if (mmio_r32(b + 0x34) < QSIZE) return false;
    uint64_t p1 = pmm_alloc(), p2 = pmm_alloc(), p3 = pmm_alloc();
    if (!p1 || !p2 || !p3) return false;
    desc = (struct vdesc *)p1; avail = (struct vavail *)(p1 + 2048); used = (struct vused *)p2; events = (struct vevent *)p3;
    for (int i = 0; i < QSIZE; i++) {
        desc[i] = (struct vdesc){ (uint64_t)&events[i], sizeof(struct vevent), 2 /* device writes */, 0 };
        avail->ring[i] = (uint16_t)i;
    }
    avail->idx = QSIZE;
    mmio_w32(b + 0x38, QSIZE);
    mmio_w32(b + 0x80, (uint32_t)p1); mmio_w32(b + 0x84, (uint32_t)(p1 >> 32));
    mmio_w32(b + 0x90, (uint32_t)(p1 + 2048)); mmio_w32(b + 0x94, (uint32_t)((p1 + 2048) >> 32));
    mmio_w32(b + 0xa0, (uint32_t)p2); mmio_w32(b + 0xa4, (uint32_t)(p2 >> 32));
    mmio_w32(b + 0x44, 1);                                       /* queue ready */
    mmio_w32(b + 0x70, 1 | 2 | 8 | 4);                           /* DRIVER_OK */
    dsb();
    mmio_w32(b + 0x50, 0);
    return true;
}

int virtio_scan(void) {
    int n = 0;
    for (int i = 0; i < 32; i++) {
        uint64_t b = VIRTIO_BASE + i * 0x200;
        if (mmio_r32(b) != 0x74726976 || mmio_r32(b + 8) == 0) continue;   /* "virt", device present */
        uint32_t ver = mmio_r32(b + 4), id = mmio_r32(b + 8);
        kprintf("virtio-mmio slot %d: device %u (transport v%u)\n", i, id, ver);
        n++;
        if (id == 18 && ver == 2 && !kbd_base && input_init(b)) {
            kbd_base = b; kbd_irq_id = 48 + i; kbd_ok = true;
            gic_enable(kbd_irq_id);
            kprintf("virtio-input: keyboard ready (irq %d)\n", kbd_irq_id);
        } else if (id == 18 && ver != 2) kprintf("virtio-input: legacy transport not supported (use -global virtio-mmio.force-legacy=false)\n");
    }
    return n;
}

void virtio_irq(int intid) {
    if (intid != kbd_irq_id) return;
    mmio_w32(kbd_base + 0x64, mmio_r32(kbd_base + 0x60));        /* acknowledge */
    dsb();
    while (last_used != used->idx) {
        uint32_t id = used->ring[last_used % QSIZE].id;
        if (id < QSIZE) {
            struct vevent e = events[id];
            if (e.type == 1) input_key(e.code, e.value != 0);    /* EV_KEY: 0 up, 1 down, 2 repeat */
            avail->ring[avail->idx % QSIZE] = (uint16_t)id;
            dsb();
            avail->idx++;
        }
        last_used++;
    }
    dsb();
    mmio_w32(kbd_base + 0x50, 0);
}
