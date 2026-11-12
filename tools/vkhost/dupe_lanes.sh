#!/bin/sh
# Frame duping lanes for the Vulkan renderer, run headless through vkhost
# (lavapipe works). Content is gen_dupe_exe.py: a still picture, a quad
# drawn each frame outside the display area, the display turned off
# part way through, then VRAM read back.
#   - Frame Duping on: a frame with nothing new on the display reaches
#     the frontend as NULL, most of the run; off: none does;
#   - what frames sent as NULL draw reaches VRAM: the next frame's
#     sprite samples it, and the readback holds the quad in the last
#     pass's colour and the sprite in the pass before's, with the
#     software framebuffer off;
#   - a new picture painted into the display area, and the display
#     turned off, each get their frame out: the frontend's frames at 90
#     (white), 120 (grey) and the last (black), and every frame between,
#     are the ones the run without duping shows;
#   - through the composite cable, whose subcarrier moves every field,
#     no frame is sent as NULL and every one matches the run without
#     duping;
#   - the validation layer reports nothing.
#
# Usage (from the repository root, after a HAVE_HW=1 build):
#   sh tools/vkhost/dupe_lanes.sh [core.so]
set -e
cd "$(dirname "$0")/../.."
CORE="${1:-./mednafen_psx_hw_libretro.so}"
case "$CORE" in /*) ;; *) CORE="$PWD/$CORE" ;; esac
OUT=/tmp/vk_dupe_lanes
SYS=/tmp/vkhost_sys
P=beetle_psx_hw
FRAMES=240

make -s -C tools/vkhost vkhost
rm -rf "$OUT"
mkdir -p "$OUT" "$SYS"
[ -f "$SYS/scph5501.bin" ] || head -c 524288 /dev/zero > "$SYS/scph5501.bin"
python3 tools/glhost/gen_dupe_exe.py "$OUT/dupe.exe"

run()
{
   # $1 name, $2 GLHOST_VARS
   VKHOST_DUMP_INTERVAL=1 VKHOST_RAM_OUT="$OUT/$1.bin:100000:100000" \
   VKHOST_VARS="${P}_renderer=hardware_vk;${P}_renderer_software_fb=disabled;$2" \
      tools/vkhost/vkhost "$CORE" "$OUT/dupe.exe" - "$FRAMES" "$OUT/$1" \
      > "$OUT/$1.log" 2>&1 || { tail -20 "$OUT/$1.log"; exit 1; }
}
run on       "${P}_frame_duping=enabled"
run off      "${P}_frame_duping=disabled"
run comp_on  "${P}_frame_duping=enabled;${P}_video_cable=composite"
run comp_off "${P}_frame_duping=disabled;${P}_video_cable=composite"

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
for run in ('off', 'on', 'comp_off', 'comp_on'):
    log = open('%s/%s.log' % (out, run)).read()
    duped = int(re.search(r'duped: (\d+) frames', log).group(1))
    errors = int(re.search(r'(\d+) validation errors', log).group(1))
    vram = struct.unpack('<%dH' % (1024 * 512), open('%s/%s.bin' % (out, run), 'rb').read()[:2 * 1024 * 512])
    quad = [vram[y * 1024 + x] for y in range(480, 496) for x in range(960, 976)]
    sprite = [vram[y * 1024 + x] for y in range(300, 316) for x in range(600, 616)]
    bad = sum(1 for v in quad if v != want) + sum(1 for v in sprite if v != want_sprite)
    every = [ppm('%s/%s/frame_%04d.ppm' % (out, run, f)) for f in range(1, frames + 1)]
    shots = [every[89], every[119], every[frames - 1]]
    if run.endswith('off'):
        ref = every
    differ = [f + 1 for f in range(frames) if every[f] != ref[f]]
    same = not differ
    white, grey, last = (max(x) for x in shots)
    dupe_ok = duped >= frames // 2 if run == 'on' else duped == 0
    print('%s: %d of %d frames sent as NULL; %d of 512 quad and sprite words differ '
          'from 0x%04x and 0x%04x; frames 90/120/last peak at %d/%d/%d, %s the run without duping'
          % (run, duped, frames, bad, want, want_sprite, white, grey, last,
             'as' if same else 'NOT as'))
    if differ:
        print('%s: frames that differ: %s' % (run, differ[:16]))
    print('%s: %d validation errors' % (run, errors))
    ok &= dupe_ok and bad == 0 and same
    ok &= errors == 0 and (run.startswith('comp') or white > grey > last == 0)
print('dupe_lanes: %s' % ('PASS' if ok else 'FAIL'))
sys.exit(0 if ok else 1)
PY
