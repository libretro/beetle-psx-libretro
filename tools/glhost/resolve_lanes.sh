#!/bin/sh
# End-of-frame resolve lanes for the OpenGL renderer, run headless through
# glhost (llvmpipe works). At the end of a frame the renderer brings the
# native copy of VRAM up to date with what the frame rendered. Content is
# gen_resolve_exe.py: one pass a frame of two quads and a 4 bpp sprite,
# far apart in VRAM, the sprite's texels the words the previous frame
# rendered.
#   - VRAM read back at the end matches a model exactly, at 1x with the
#     software framebuffer on and at 4x with it off;
#   - the resolve covers the 8x8 tiles the frame wrote, not the bounds
#     around them: glhost counts the pixels of the renderer's rect draws,
#     held to 4096 a frame where the bounds take most of VRAM.
#
# Usage (from the repository root, after a HAVE_HW=1 build):
#   sh tools/glhost/resolve_lanes.sh [core.so]
set -e
cd "$(dirname "$0")/../.."
CORE="${1:-./mednafen_psx_hw_libretro.so}"
case "$CORE" in /*) ;; *) CORE="$PWD/$CORE" ;; esac
OUT=/tmp/gl_resolve_lanes
SYS=/tmp/vkhost_sys
P=beetle_psx_hw
FRAMES=360

make -s -C tools/glhost glhost
rm -rf "$OUT"
mkdir -p "$OUT" "$SYS"
[ -f "$SYS/scph5501.bin" ] || head -c 524288 /dev/zero > "$SYS/scph5501.bin"
python3 tools/glhost/gen_resolve_exe.py "$OUT/resolve.exe"

run()
{
   # $1 name, $2 GLHOST_VARS
   GLHOST_RAM_OUT="$OUT/$1.bin:100000:100000" \
   GLHOST_VARS="${P}_renderer=hardware_gl;$2" \
      tools/glhost/glhost "$CORE" "$OUT/resolve.exe" - "$FRAMES" "$OUT/$1" \
      > "$OUT/$1.log" 2>&1 || { tail -20 "$OUT/$1.log"; exit 1; }
}
run sw1x "${P}_renderer_software_fb=enabled"
run hw4x "${P}_renderer_software_fb=disabled;${P}_internal_resolution=4x"

python3 - "$OUT" "$FRAMES" <<'PY'
import re, struct, sys
out, frames = sys.argv[1], int(sys.argv[2])
def p(i): return (i ^ (i >> 5) ^ (i >> 11)) & 0xffff
def c(n): q = n & 31; return q | (q << 5)
clut = [p(500 * 1024 + i) for i in range(16)]
model = [p(i) for i in range(1024 * 512)]
for y in range(8, 24):
    for x in range(40, 56):
        t = clut[(c(249) >> (4 * ((x - 40) & 3))) & 15]
        if t:
            model[y * 1024 + x] = t
for y0, x0 in ((8, 8), (480, 960)):
    for y in range(y0, y0 + 16):
        for x in range(x0, x0 + 16):
            model[y * 1024 + x] = c(250)
ok = True
for run in ('sw1x', 'hw4x'):
    got = struct.unpack('<%dH' % len(model), open('%s/%s.bin' % (out, run), 'rb').read()[:2 * len(model)])
    bad = sum(1 for a, b in zip(got, model) if a != b)
    px = int(re.search(r'(\d+) pixels of rect draws', open('%s/%s.log' % (out, run)).read()).group(1))
    limit = 4096 * (frames // 2)
    print('%s: %d of %d words differ from the model; %d pixels of rect draws in the last %d frames (limit %d)'
          % (run, bad, len(model), px, frames // 2, limit))
    ok &= bad == 0 and px <= limit
print('resolve_lanes: %s' % ('PASS' if ok else 'FAIL'))
sys.exit(0 if ok else 1)
PY
