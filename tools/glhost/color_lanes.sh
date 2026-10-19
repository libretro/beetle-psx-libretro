#!/bin/sh
# Hardware color lanes for the OpenGL renderer, run headless through glhost
# (surfaceless EGL; llvmpipe works). Content is gen_color_exe.py; each run
# dumps VRAM (0,0)-(255,255) as the content reads it back through GP0(C0h).
#
#   1. Software renderer: the reference.
#   2. GL 1x, software framebuffer off: every native average cell equals
#      (S + D) >> 1 and the triangle matches the reference.
#   3. GL 4x with the software framebuffer on, switched to 2x mid-run: the
#      framebuffer rebuild must keep the software VRAM, so the triangle
#      still matches the reference.
#
# Usage (from the repository root, after a HAVE_HW=1 build):
#   sh tools/glhost/color_lanes.sh [core.so]
set -e
cd "$(dirname "$0")/../.."
CORE="${1:-./mednafen_psx_hw_libretro.so}"
OUT=/tmp/glhost_color_lanes
SYS=/tmp/vkhost_sys
P=beetle_psx_hw
FRAMES=300

make -s -C tools/glhost glhost
mkdir -p "$OUT" "$SYS"
[ -f "$SYS/scph5501.bin" ] || head -c 524288 /dev/zero > "$SYS/scph5501.bin"
python3 tools/glhost/gen_color_exe.py "$OUT/color.exe"

run()
{
   # $1 name, $2 GLHOST_VARS, $3 GLHOST_VARS_AT
   GLHOST_VARS="$2" GLHOST_VARS_AT="$3" \
   GLHOST_RAM_OUT="$OUT/$1.bin:100000:20000" \
      tools/glhost/glhost "$CORE" "$OUT/color.exe" - "$FRAMES" "$OUT/$1" \
      2> "$OUT/$1.log" || { cat "$OUT/$1.log"; exit 1; }
}

run sw "${P}_renderer=software" ""
run gl1x "${P}_renderer_software_fb=disabled" ""
run gl4to2 "${P}_internal_resolution=4x" "150:${P}_internal_resolution=2x"

python3 - "$OUT" <<'EOF'
import struct, sys
out = sys.argv[1]
def load(name):
    d = open('%s/%s.bin' % (out, name), 'rb').read()
    return struct.unpack('<%dH' % (len(d) // 2), d)
ref = load('sw')
def average_wrong(v):
    bad = 0
    for s in range(32):
        for d in range(32):
            w = v[(s * 4 + 1) * 256 + d * 4 + 1]
            e = (s + d) >> 1
            bad += (w & 31, (w >> 5) & 31, (w >> 10) & 31) != (e, e, e)
    return bad
def triangle_diff(v):
    return sum(1 for y in range(256) for x in range(128, 256)
               if v[y * 256 + x] != ref[y * 256 + x])
fail = 0
for name, check_average in (('sw', True), ('gl1x', True), ('gl4to2', False)):
    v = load(name)
    a = average_wrong(v) if check_average else 0
    t = triangle_diff(v)
    ok = a == 0 and t == 0
    fail |= not ok
    print('%-7s average cells wrong: %4d  triangle pixels differing: %5d  %s'
          % (name, a, t, 'OK' if ok else 'FAIL'))
sys.exit(1 if fail else 0)
EOF
