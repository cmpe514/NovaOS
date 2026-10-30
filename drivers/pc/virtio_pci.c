/* virtio over PCI, legacy (transitional) I/O-port interface, for the block device (QEMU: virtio-blk-pci,
 * disable-modern=on). The queue lives in kernel memory, which is identity mapped, so its address is physical. */
#include "kernel.h"
#include "virtio.h"

uint32_t pci_read(int bus, int dev, int fn, int off);
void pci_write(int bus, int dev, int fn, int off, uint32_t v);

static uint16_t io;
static uint8_t ring[3 * 4096] __attribute__((aligned(4096)));
static virtq_t q;
static void notify(void) { outw(io + 0x10, 0); }

void virtio_pci_probe(int bus, int dev, int fn, uint16_t device) {
    if (device != 0x1001 || io) return;                         /* legacy virtio-blk */
    uint32_t bar0 = pci_read(bus, dev, fn, 0x10);
    if (!(bar0 & 1)) { kprintf("virtio-blk: no legacy I/O BAR (use disable-modern=on)\n"); return; }
    io = bar0 & 0xfffc;
    pci_write(bus, dev, fn, 4, pci_read(bus, dev, fn, 4) | 0x5);   /* I/O space + bus master */
    outb(io + 0x12, 0);
    outb(io + 0x12, 1 | 2);                                     /* ACKNOWLEDGE | DRIVER */
    uint32_t feat = inl(io + 0x00);
    outl(io + 0x04, feat & (1u << 9));                          /* only VIRTIO_BLK_F_FLUSH */
    outw(io + 0x0e, 0);
    uint16_t n = inw(io + 0x0c);
    uint64_t used_off = ((16ull * n + 6 + 2ull * n) + 4095) & ~4095ull;
    if (!n || used_off + 6 + 8ull * n > sizeof ring) { kprintf("virtio-blk: queue size %u unsupported\n", n); io = 0; return; }
    q = (virtq_t){ (struct vdesc *)ring, (struct vavail *)(ring + 16 * n), (struct vused *)(ring + used_off), n, 0, notify };
    outl(io + 0x08, (uint32_t)((uint64_t)ring >> 12));
    outb(io + 0x12, 1 | 2 | 4);                                 /* DRIVER_OK */
    uint64_t sectors = inl(io + 0x14) | (uint64_t)inl(io + 0x18) << 32;
    virtio_blk_attach(&q, sectors, feat & (1u << 9));
}
