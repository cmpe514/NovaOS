/* The NovaOS data store: crash-safe A/B snapshots on the data disk.
 *
 * Disk layout (512-byte sectors):  0: label "NOVAOS-DATA-DISK"   8..135: slot A   136..263: slot B
 * A slot = header sector (magic, generation, length, CRC-32 of data, CRC-32 of header) + up to 127 data sectors.
 * A write goes to the slot NOT holding the current snapshot: data first, flush, then the header, flush. A torn
 * or failed write therefore leaves a slot whose checksum fails, and the previous snapshot stays current. */
#include "kernel.h"

bool blk_io(uint64_t sector, void *buf, uint32_t count, bool write);
bool blk_flush(void);
extern bool blk_present;
extern uint64_t blk_sectors;

#define LABEL "NOVAOS-DATA-DISK"
#define SLOT0 8
#define SLOT_SECTORS 128
typedef struct { char magic[8]; uint32_t version, length; uint64_t generation; uint32_t data_crc, hdr_crc; } hdr_t;

static uint8_t data[STORE_MAX] __attribute__((aligned(16)));
static uint8_t sec[512] __attribute__((aligned(16)));
static nova_store_info info;

static uint32_t crc32(const void *p, size_t n) {
    uint32_t c = 0xffffffff;
    for (size_t i = 0; i < n; i++) {
        c ^= ((const uint8_t *)p)[i];
        for (int k = 0; k < 8; k++) c = c >> 1 ^ (0xedb88320 & -(c & 1));
    }
    return ~c;
}
static void set_status(const char *s) { size_t n = strlen(s); memcpy(info.status, s, n < 47 ? n + 1 : 47); }

/* Reads slot `s`; on success leaves its data in `data` and returns its header. */
static bool load_slot(int s, hdr_t *h) {
    uint64_t base = SLOT0 + (uint64_t)s * SLOT_SECTORS;
    if (!blk_io(base, sec, 1, false)) return false;
    memcpy(h, sec, sizeof *h);
    if (memcmp(h->magic, "NOVASNAP", 8) || h->version != 1 || h->length > STORE_MAX ||
        crc32(h, offsetof(hdr_t, hdr_crc)) != h->hdr_crc) return false;
    if (h->length && !blk_io(base + 1, data, (h->length + 511) / 512, false)) return false;
    return crc32(data, h->length) == h->data_crc;
}

void store_init(void) {
    memset(&info, 0, sizeof info);
    if (!blk_present) { set_status("No data disk attached"); return; }
    info.disk_bytes = blk_sectors * 512;
    if (blk_sectors < SLOT0 + 2 * SLOT_SECTORS) { set_status("Data disk too small (needs 132 KiB)"); return; }
    if (!blk_io(0, sec, 1, false)) { set_status("Data disk read error"); return; }
    if (memcmp(sec, LABEL, sizeof LABEL - 1)) {
        for (int i = 0; i < 512; i++)
            if (sec[i]) { set_status("Disk has unknown contents: not used"); kprintf("store: disk is not a NovaOS data disk and not blank; refusing to use it\n"); return; }
        memset(sec, 0, 512); memcpy(sec, LABEL, sizeof LABEL - 1);
        if (!blk_io(0, sec, 1, true) || !blk_flush()) { set_status("Data disk write error"); return; }
        kprintf("store: blank disk labelled as a NovaOS data disk\n");
    }
    info.present = 1;
    hdr_t h[2]; uint64_t best_gen = 0; int best = -1;
    for (int s = 0; s < 2; s++) {
        info.valid[s] = load_slot(s, &h[s]);
        kprintf("store: slot %c %s gen=%lu\n", 'A' + s, info.valid[s] ? "valid" : "empty/invalid", info.valid[s] ? h[s].generation : 0);
        if (info.valid[s] && (best < 0 || h[s].generation > best_gen)) { best = s; best_gen = h[s].generation; }
    }
    if (best < 0) { set_status("Ready (no saved data yet)"); return; }
    if (!load_slot(best, &h[best])) { set_status("Read error"); info.present = 0; return; }
    info.has_data = 1; info.slot = best; info.generation = best_gen; info.length = h[best].length;
    set_status("Ready");
    kprintf("NOVA_STORE_LOADED slot=%c gen=%lu bytes=%u\n", 'A' + best, best_gen, info.length);
}

void store_get_info(nova_store_info *out) { *out = info; }

int64_t store_read(void *dst, uint64_t max) {
    if (!info.present) return -E_NOENT;
    if (!info.has_data) return 0;
    uint64_t n = info.length < max ? info.length : max;
    memcpy(dst, data, n);
    return (int64_t)n;
}

int64_t store_write(const void *src, uint64_t len) {
    if (!info.present) return -E_NOENT;
    if (len > STORE_MAX) return -E_INVAL;
    int s = info.has_data ? !info.slot : 0;
    uint64_t base = SLOT0 + (uint64_t)s * SLOT_SECTORS;
    memset(data, 0, sizeof data);
    memcpy(data, src, len);
    hdr_t h = { "NOVASNAP", 1, (uint32_t)len, info.generation + 1, crc32(data, len), 0 };
    h.hdr_crc = crc32(&h, offsetof(hdr_t, hdr_crc));
    memset(sec, 0, 512); memcpy(sec, &h, sizeof h);
    if (!blk_io(base + 1, data, (uint32_t)((len + 511) / 512), true) || !blk_flush() ||
        !blk_io(base, sec, 1, true) || !blk_flush()) {
        kprintf("store: write to slot %c failed; previous snapshot kept\n", 'A' + s);
        info.valid[s] = 0;
        return -E_BUSY;
    }
    info.valid[s] = 1; info.has_data = 1; info.slot = s; info.generation = h.generation; info.length = (uint32_t)len;
    kprintf("NOVA_STORE_SAVED slot=%c gen=%lu bytes=%lu\n", 'A' + s, h.generation, len);
    return (int64_t)len;
}

int64_t store_erase(void) {
    if (!info.present) return -E_NOENT;
    memset(sec, 0, 512);
    for (int s = 0; s < 2; s++) if (!blk_io(SLOT0 + (uint64_t)s * SLOT_SECTORS, sec, 1, true)) return -E_BUSY;
    if (!blk_flush()) return -E_BUSY;
    info.has_data = 0; info.valid[0] = info.valid[1] = 0; info.generation = 0; info.length = 0;
    kprintf("NOVA_STORE_ERASED\n");
    return 0;
}
