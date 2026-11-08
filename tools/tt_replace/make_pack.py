#!/usr/bin/env python3
"""Builds a synthetic HD replacement pack from a texture dump, no game
content: each dumped PNG is scaled 4x with a per-texel pattern, so every
mip level of the replacement differs from the native texture.
usage: make_pack.py dump_dir replacements_dir"""
import os, struct, sys, zlib
src_dir, dst_dir = sys.argv[1], sys.argv[2]
def read_png(path):
    d = open(path, 'rb').read()
    pos, idat = 8, b''
    while pos < len(d):
        n, t = struct.unpack('>I4s', d[pos:pos + 8])
        body = d[pos + 8:pos + 8 + n]
        if t == b'IHDR':
            w, h, depth, ctype = struct.unpack('>IIBB', body[:10])
            assert depth == 8 and ctype == 6, 'dumps are RGBA8'
        elif t == b'IDAT':
            idat += body
        pos += 12 + n
    raw, stride, rows, prev = zlib.decompress(idat), w * 4, [], bytearray(w * 4)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - 4] if i >= 4 else 0
            b = prev[i]
            c = prev[i - 4] if i >= 4 else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line); prev = line
    return w, h, rows
def write_png(path, w, h, rows):
    def chunk(t, b): return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)
    raw = b''.join(b'\0' + bytes(r) for r in rows)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))
os.makedirs(dst_dir, exist_ok=True)
names = sorted(n for n in os.listdir(src_dir) if n.endswith('.png'))
assert names, 'nothing was dumped'
S = 4
for name in names:
    w, h, rows = read_png(os.path.join(src_dir, name))
    big = []
    for y in range(h * S):
        s, r = rows[y // S], bytearray(w * S * 4)
        for x in range(w * S):
            i = (x // S) * 4
            px = s[i:i + 4]
            if px[3]:   # transparency as it is; the colour varies per HD texel
                k = (x * 5 + y * 3) & 15
                px = bytes(((px[0] ^ (k << 3)) | 1, (px[1] ^ (k << 2)) | 1, (px[2] ^ (k << 4)) | 1, px[3]))
            r[x * 4:x * 4 + 4] = px
        big.append(r)
    write_png(os.path.join(dst_dir, name), w * S, h * S, big)
    print('pack: %s %dx%d -> %dx%d' % (name, w, h, w * S, h * S))
