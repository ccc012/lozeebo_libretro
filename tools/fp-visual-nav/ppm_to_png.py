#!/usr/bin/env python3
"""Convert PPM P6 to PNG via stdlib zlib (no pillow)."""
from __future__ import annotations
import struct, zlib, sys
from pathlib import Path

def read_ppm(path: Path):
    raw = path.read_bytes()
    i = nl = 0
    while nl < 3 and i < len(raw):
        if raw[i] == 10: nl += 1
        i += 1
    parts = raw[:i].decode().split()
    w, h = int(parts[1]), int(parts[2])
    return w, h, raw[i:i + w * h * 3]

def write_png(path: Path, w: int, h: int, rgb: bytes):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))

def main():
    for arg in sys.argv[1:]:
        p = Path(arg)
        w, h, rgb = read_ppm(p)
        out = p.with_suffix(".png")
        write_png(out, w, h, rgb)
        print(out)

if __name__ == "__main__":
    main()
