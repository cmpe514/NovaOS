#!/usr/bin/env python3
"""screen-check.py IN.ppm OUT.png [MIN_COLORS]

Converts a QEMU screendump (binary PPM) to PNG and fails if the frame looks blank: fewer than MIN_COLORS
(default 24) distinct colours in a sample grid, or almost no variation. Standard library only."""
import struct, sys, zlib

def read_ppm(path):
    data = open(path, "rb").read()
    parts, pos = [], 0
    while len(parts) < 4:
        while data[pos:pos+1].isspace(): pos += 1
        if data[pos:pos+1] == b"#":
            pos = data.index(b"\n", pos) + 1; continue
        end = pos
        while not data[end:end+1].isspace(): end += 1
        parts.append(data[pos:end]); pos = end
    assert parts[0] == b"P6", "not a binary PPM"
    w, h, maxval = int(parts[1]), int(parts[2]), int(parts[3])
    assert maxval == 255
    return w, h, data[pos+1:pos+1+w*h*3]

def write_png(path, w, h, rgb):
    raw = b"".join(b"\0" + rgb[y*w*3:(y+1)*w*3] for y in range(h))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)

w, h, rgb = read_ppm(sys.argv[1])
write_png(sys.argv[2], w, h, rgb)
min_colors = int(sys.argv[3]) if len(sys.argv) > 3 else 24
colors, values = set(), []
for y in range(0, h, max(1, h // 60)):
    for x in range(0, w, max(1, w // 80)):
        i = (y * w + x) * 3
        px = rgb[i:i+3]; colors.add(px); values.append(sum(px))
mean = sum(values) / len(values)
spread = (sum((v - mean) ** 2 for v in values) / len(values)) ** 0.5
print(f"{w}x{h}, {len(colors)} distinct colours in the sample, brightness spread {spread:.1f}")
sys.exit(0 if len(colors) >= min_colors and spread >= 2 else 1)
