/* Split virtqueue structures shared by the virtio transports (PCI legacy on x86, MMIO on QEMU virt). */
#pragma once
#include <stdint.h>
struct vdesc { uint64_t addr; uint32_t len; uint16_t flags, next; };
struct vavail { uint16_t flags, idx, ring[]; };
struct vused { uint16_t flags, idx; struct { uint32_t id, len; } ring[]; };
#define VD_NEXT 1
#define VD_WRITE 2
typedef struct {
    struct vdesc *desc; struct vavail *avail; struct vused *used;
    uint16_t size, last_used;
    void (*notify)(void);
} virtq_t;
/* Called by a transport once the block device's request queue is live. */
void virtio_blk_attach(virtq_t *q, uint64_t sectors, bool can_flush);
