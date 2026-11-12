#!/bin/sh
# Frame duping lanes for the OpenGL renderer, run headless through glhost
# (llvmpipe works). Content is gen_dupe_exe.py: a still picture, a quad
# drawn each frame outside the display area, the display turned off
# part way through, then VRAM read back.
#   - Frame Duping on: a frame with nothing new on the display reaches
#     the frontend as NULL, most of the run; off: none does;
#   - what frames sent as NULL draw reaches the native copy of VRAM:
#     the next frame's sprite samples it, and the readback holds the
#     quad in the last pass's colour and the sprite in the pass before's,
#     with the software framebuffer off, in both runs;
#   - the display turned off on that still picture gets a black frame
#     out: the frontend's last frame is black, after a white one.
#
# Usage (from the repository root, after a HAVE_HW=1 build):
#   sh tools/glhost/dupe_lanes.sh [core.so]
set -e
cd "$(dirname "$0")/../.."
CORE="${1:-./mednafen_psx_hw_libretro.so}"
case "$CORE" in /*) ;; *) CORE="$PWD/$CORE" ;; esac
OUT=/tmp/gl_dupe_lanes
SYS=/tmp/vkhost_sys
P=beetle_psx_hw
FRAMES=240

make -s -C tools/glhost glhost
rm -rf "$OUT"
mkdir -p "$OUT" "$SYS"
[ -f "$SYS/scph5501.bin" ] || head -c 524288 /dev/zero > "$SYS/scph5501.bin"
python3 tools/glhost/gen_dupe_exe.py "$OUT/dupe.exe"

run()
{
   # $1 name, $2 GLHOST_VARS
   GLHOST_RAM_OUT="$OUT/$1.bin:100000:100000" \
   GLHOST_VARS="${P}_renderer=hardware_gl;${P}_renderer_software_fb=disabled;$2" \
      tools/glhost/glhost "$CORE" "$OUT/dupe.exe" - "$FRAMES" "$OUT/$1" \
      > "$OUT/$1.log" 2>&1 || { tail -20 "$OUT/$1.log"; exit 1; }
}
run on  "${P}_frame_duping=enabled"
run off "${P}_frame_duping=disabled"

python3 - "$OUT" "$FRAMES" <<'PY'
import re, struct, sys
out, frames = sys.argv[1], int(sys.argv[2])
def colour(p):
    c = (8 * (p & 31) + 7) >> 3
    return c | (c << 5)
want, want_sprite = colour(200), colour(199)
def ppm(path):
    d = open(path, 'rb').read()
    m = re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s', d)
    return d[m.end():]
ok = True
for run in ('on', 'off'):
    log = open('%s/%s.log' % (out, run)).read()
    duped = int(re.search(r'duped: (\d+) frames', log).group(1))
    vram = struct.unpack('<%dH' % (1024 * 512), open('%s/%s.bin' % (out, run), 'rb').read()[:2 * 1024 * 512])
    quad = [vram[y * 1024 + x] for y in range(480, 496) for x in range(960, 976)]
    sprite = [vram[y * 1024 + x] for y in range(300, 316) for x in range(600, 616)]
    bad = sum(1 for v in quad if v != want) + sum(1 for v in sprite if v != want_sprite)
    white = max(ppm('%s/%s/frame_0090.ppm' % (out, run)))
    last = max(ppm('%s/%s/frame_%04d.ppm' % (out, run, frames)))
    dupe_ok = duped >= frames // 2 if run == 'on' else duped == 0
    print('%s: %d of %d frames sent as NULL; %d of 512 quad and sprite words differ '
          'from 0x%04x and 0x%04x; frame 90 peaks at %d, the last frame at %d'
          % (run, duped, frames, bad, want, want_sprite, white, last))
    ok &= dupe_ok and bad == 0 and white == 255 and last == 0
print('dupe_lanes: %s' % ('PASS' if ok else 'FAIL'))
sys.exit(0 if ok else 1)
PY
