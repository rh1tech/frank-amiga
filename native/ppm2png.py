#!/usr/bin/env python3
"""Tiny dependency-free binary-PPM (P6) -> PNG converter."""
import sys, zlib, struct

def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:2] == b"P6", "not a P6 PPM"
    # parse header: P6 <w> <h> <maxval>\n  (whitespace separated, may have comments)
    idx = 2
    fields = []
    while len(fields) < 3:
        while data[idx] in b" \t\r\n":
            idx += 1
        if data[idx:idx+1] == b"#":
            while data[idx] not in b"\r\n":
                idx += 1
            continue
        start = idx
        while data[idx] not in b" \t\r\n":
            idx += 1
        fields.append(int(data[start:idx]))
    idx += 1  # single whitespace after maxval
    w, h, _maxv = fields
    return w, h, data[idx:idx + w*h*3]

def write_png(path, w, h, rgb):
    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & 0xffffffff))
    raw = bytearray()
    stride = w*3
    for y in range(h):
        raw.append(0)
        raw.extend(rgb[y*stride:(y+1)*stride])
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)

if __name__ == "__main__":
    src, dst = sys.argv[1], sys.argv[2]
    w, h, rgb = read_ppm(src)
    write_png(dst, w, h, rgb)
    print(f"{src} -> {dst} ({w}x{h})")
