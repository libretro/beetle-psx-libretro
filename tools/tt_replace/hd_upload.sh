#!/bin/sh
# HD replacement uploads through the Vulkan renderer, run headless through
# vkhost (lavapipe works). Builds a small synthetic replacement pack - no
# game content: the texture a generated PS-X EXE uploads is dumped, then
# scaled 4x with a per-texel pattern, so every mip level of the replacement
# differs from the native texture - and checks:
#   - the pack is used (frames differ from the run without it);
#   - frames are identical to a reference core's, with the pack loaded
#     synchronously (every frame) and asynchronously (last frame);
#   - the bytes the core memcpy's on the render thread for the replacement
#     uploads: per call site in the core, the difference between the runs
#     with and without the pack (the driver's own copies are left out).
#
# Usage (from the repository root, after a HAVE_HW=1 build):
#   sh tools/tt_replace/hd_upload.sh [core.so [reference.so [max_bytes]]]
# max_bytes bounds the core's render-thread memcpy bytes per replaced
# texture (default: report only).
set -e
cd "$(dirname "$0")/../.."
CORE="${1:-./mednafen_psx_hw_libretro.so}"
REF="${2:-}"
MAX="${3:-}"
OUT=/tmp/tt_hd_upload
SYS=/tmp/vkhost_sys
P=beetle_psx_hw
FRAMES=30

make -s -C tools/vkhost vkhost
rm -rf "$OUT"
mkdir -p "$OUT/content" "$SYS"
[ -f "$SYS/scph5501.bin" ] || head -c 524288 /dev/zero > "$SYS/scph5501.bin"
python3 tools/rhi_coherence/gen_frame_exe.py "$OUT/content/hd.exe" static
${CC:-cc} -O2 -shared -fPIC -o "$OUT/memcpy_count.so" tools/tt_replace/memcpy_count.c -ldl

run()
{
   # $1 core, $2 name, $3 replace (enabled/disabled), $4 caching method
   VKHOST_DUMP_INTERVAL=1 VKHOST_VARS="${P}_track_textures=enabled;${P}_replace_textures=$3;${P}_dump_textures=disabled;${P}_hd_caching_method=$4" \
      LD_PRELOAD="$OUT/memcpy_count.so" MEMCPY_COUNT_SITES="$OUT/$2.sites" \
      tools/vkhost/vkhost "$1" "$OUT/content/hd.exe" - "$FRAMES" "$OUT/$2" \
      > "$OUT/$2.log" 2>&1 || { tail -20 "$OUT/$2.log"; exit 1; }
}

# The pack: dump what the EXE uploads, then make each dump's replacement.
VKHOST_VARS="${P}_track_textures=enabled;${P}_dump_textures=enabled;${P}_replace_textures=disabled" \
   tools/vkhost/vkhost "$CORE" "$OUT/content/hd.exe" - "$FRAMES" "$OUT/dump" > "$OUT/dump.log" 2>&1 \
   || { tail -20 "$OUT/dump.log"; exit 1; }
python3 - "$OUT/content/hd-texture-dump" "$OUT/content/hd-texture-replacements" <<'EOF'
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
EOF

run "$CORE" native disabled lazy_sync
run "$CORE" sync enabled lazy_sync
run "$CORE" async enabled lazy
if [ -n "$REF" ]; then
   run "$REF" ref_native disabled lazy_sync
   run "$REF" ref_sync enabled lazy_sync
   run "$REF" ref_async enabled lazy
fi

python3 - "$OUT" "$FRAMES" "$CORE" "$REF" "$MAX" <<'EOF'
import bisect, collections, os, subprocess, sys
out, frames, core, ref, mx = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4], sys.argv[5]
def frame(run, i): return open('%s/%s/frame_%04d.ppm' % (out, run, i), 'rb').read()
def core_memcpy(run, so):
    # bytes per function of the core, from the call sites' offsets
    syms = []
    for l in subprocess.run(['nm', '-n', '--defined-only', so], capture_output=True, text=True).stdout.splitlines():
        f = l.split()
        if len(f) >= 3 and f[1] in 'tTwW': syms.append((int(f[0], 16), f[2]))
    addrs, name = [a for a, _ in syms], os.path.basename(so)
    per = collections.Counter()
    for l in open('%s/%s.sites' % (out, run)):
        obj, off, b, n = l.split()
        if os.path.basename(obj) == name:
            i = bisect.bisect_right(addrs, int(off, 16)) - 1
            per[syms[i][1] if i >= 0 else '?'] += int(b)
    return per
ok = True
if frame('sync', frames) == frame('native', frames):
    print('FAIL: the replacement pack was not used'); ok = False
if ref:
    bad = [i for i in range(1, frames + 1) if frame('sync', i) != frame('ref_sync', i)]
    print('sync: %d/%d frames identical to the reference' % (frames - len(bad), frames))
    if bad: print('FAIL: sync frames differ from the reference: %s' % bad[:10]); ok = False
    if frame('async', frames) != frame('ref_async', frames):
        print('FAIL: async last frame differs from the reference'); ok = False
    else:
        print('async: last frame identical to the reference')
n = len(os.listdir(os.path.join(out, 'content', 'hd-texture-replacements')))
for label, prefix, so in [('core', '', core)] + ([('reference', 'ref_', ref)] if ref else []):
    on, off = core_memcpy(prefix + 'sync', so), core_memcpy(prefix + 'native', so)
    delta = dict((k, on[k] - off[k]) for k in set(on) | set(off) if on[k] != off[k])
    total = sum(delta.values())
    print('%s: render-thread memcpy for %d replaced texture(s): %d bytes per texture%s'
          % (label, n, total // n, ''.join('\n   %10d %s' % (v, k) for k, v in
             sorted(delta.items(), key=lambda kv: -abs(kv[1]))[:4])))
    if not prefix and mx and total // n > int(mx):
        print('FAIL: more than %s bytes per texture' % mx); ok = False
sys.exit(0 if ok else 1)
EOF
echo "hd_upload: PASS"
