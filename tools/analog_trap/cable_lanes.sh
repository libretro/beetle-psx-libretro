#!/bin/sh
# Analog cable lanes: the HDR post lanes' reference scene (white squares and
# a mid-grey one on black) through every cable tier on the Vulkan renderer
# (vkhost, lavapipe) and the OpenGL renderer (glhost, llvmpipe), 24-bit
# output. Each picture must keep its black background black and its white
# square white, and the two renderers must agree. The GL composite and RF
# tiers once came out flat grey: the chroma trap ran with unstable
# coefficients and diverged along every line.
#
# Usage (from the repository root, after a HAVE_HW=1 build):
#   sh tools/analog_trap/cable_lanes.sh [core.so]
set -e
cd "$(dirname "$0")/../.."
CORE="${1:-./mednafen_psx_hw_libretro.so}"
case "$CORE" in /*) ;; *) CORE="$PWD/$CORE" ;; esac
OUT=/tmp/analog_cable_lanes
SYS=/tmp/vkhost_sys
P=beetle_psx_hw
FRAMES=300

make -s -C tools/vkhost vkhost
make -s -C tools/glhost glhost
rm -rf "$OUT"
mkdir -p "$OUT" "$SYS"
[ -f "$SYS/scph5501.bin" ] || head -c 524288 /dev/zero > "$SYS/scph5501.bin"
python3 tools/hdr_post/gen_post_exe.py "$OUT/scene.exe"

for cable in composite rf svideo rgb; do
   VKHOST_VARS="${P}_renderer=hardware_vk;${P}_color_format=24bit;${P}_video_cable=$cable" \
      tools/vkhost/vkhost "$CORE" "$OUT/scene.exe" - "$FRAMES" "$OUT/$cable.vk" \
      > "$OUT/$cable.vk.log" 2>&1 || { cat "$OUT/$cable.vk.log"; exit 1; }
   grep -q " 0 validation errors" "$OUT/$cable.vk.log" ||
      { cat "$OUT/$cable.vk.log"; echo "$cable: Vulkan validation errors" >&2; exit 1; }
   GLHOST_VARS="${P}_renderer=hardware_gl;${P}_color_format=24bit;${P}_video_cable=$cable" \
      tools/glhost/glhost "$CORE" "$OUT/scene.exe" - "$FRAMES" "$OUT/$cable.gl" \
      > "$OUT/$cable.gl.log" 2>&1 || { cat "$OUT/$cable.gl.log"; exit 1; }
   grep -q "chain built" "$OUT/$cable.gl.log" ||
      { cat "$OUT/$cable.gl.log"; echo "$cable: the GL analog chain did not build" >&2; exit 1; }
done

python3 - "$OUT" "$FRAMES" <<'PYEOF'
import sys
out, frames = sys.argv[1], int(sys.argv[2])
fail = []

def load(name):
    d = open('%s/%s/frame_%04d.ppm' % (out, name, frames), 'rb').read()
    parts = d.split(b'\n', 3)
    w, h = map(int, parts[1].split())
    px = parts[3]
    return w, h, [[tuple(px[(y * w + x) * 3:(y * w + x) * 3 + 3]) for x in range(w)] for y in range(h)]

def region(img, x0, y0, x1, y1):
    return [img[y][x] for y in range(y0, y1) for x in range(x0, x1)]

def mean(cs):
    return sum(sum(c) / 3.0 for c in cs) / len(cs)

for cable in ('composite', 'rf', 'svideo', 'rgb'):
    imgs = {}
    for r in ('vk', 'gl'):
        w, h, img = load('%s.%s' % (cable, r))
        imgs[r] = img
        bg = mean(region(img, 200, 40, 300, 100))
        white = mean(region(img, 156, 116, 164, 124))
        if bg > 24:
            fail.append('%s.%s: black background reads %.1f' % (cable, r, bg))
        if white < 200:
            fail.append('%s.%s: white square reads %.1f' % (cable, r, white))
        print('%-9s %s: background %.1f, white %.1f' % (cable, r, bg, white))
    a, b = imgs['vk'], imgs['gl']
    d = sum(abs(p[i] - q[i]) for ra, rb in zip(a, b) for p, q in zip(ra, rb) for i in range(3))
    d /= 3.0 * len(a) * len(a[0])
    if d > 4.0:
        fail.append('%s: Vulkan and OpenGL differ by %.2f levels on average' % (cable, d))

if fail:
    print('\n'.join('FAIL ' + f for f in fail))
    sys.exit(1)
print('analog cables: all lanes passed')
PYEOF
