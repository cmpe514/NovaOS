/* virtio-blk request path (transport independent). Requests are synchronous and polled: one request at a
 * time, descriptors 0-2, data through a kernel bounce buffer so every address handed to the device is physical. */
#include "kernel.h"
#include "virtio.h"

static virtq_t *q;
static bool flush_ok;
bool blk_present;
uint64_t blk_sectors;
static struct { uint32_t type, reserved; uint64_t sector; } req __attribute__((aligned(16)));
static volatile uint8_t status;
static uint8_t bounce[4096] __attribute__((aligned(4096)));

void virtio_blk_attach(virtq_t *vq, uint64_t sectors, bool can_flush) {
    q = vq; blk_sectors = sectors; flush_ok = can_flush; blk_present = true;
    kprintf("virtio-blk: %lu KiB%s\n", sectors / 2, can_flush ? ", flush supported" : "");
}

static bool submit(uint32_t type, uint64_t sector, uint32_t bytes, bool dev_writes) {
    req.type = type; req.reserved = 0; req.sector = sector;
    status = 0xff;
    int n = 0;
    q->desc[n] = (struct vdesc){ (uint64_t)&req, sizeof req, VD_NEXT, 1 }; n++;
    if (bytes) { q->desc[n] = (struct vdesc){ (uint64_t)bounce, bytes, (uint16_t)(VD_NEXT | (dev_writes ? VD_WRITE : 0)), 2 }; n++; }
    else q->desc[0].next = 2;
    q->desc[2] = (struct vdesc){ (uint64_t)&status, 1, VD_WRITE, 0 };
    q->avail->ring[q->avail->idx % q->size] = 0;
    __asm__ volatile("" ::: "memory");
#if defined(__aarch64__)
    dsb();
#endif
    q->avail->idx++;
    q->notify();
    for (uint64_t spin = 0; *(volatile uint16_t *)&q->used->idx == q->last_used; spin++)
        if (spin > 2000000000ull) { kprintf("virtio-blk: timeout\n"); return false; }
    q->last_used++;
    return status == 0;
}

/* Read or write `count` 512-byte sectors. */
bool blk_io(uint64_t sector, void *buf, uint32_t count, bool write) {
    if (!blk_present || sector + count > blk_sectors) return false;
    for (uint32_t done = 0; done < count; ) {
        uint32_t n = count - done < 8 ? count - done : 8;
        if (write) memcpy(bounce, (uint8_t *)buf + done * 512, n * 512);
        if (!submit(write ? 1 : 0, sector + done, n * 512, !write)) return false;
        if (!write) memcpy((uint8_t *)buf + done * 512, bounce, n * 512);
        done += n;
    }
    return true;
}

bool blk_flush(void) { return !flush_ok || submit(4, 0, 0, false); }
