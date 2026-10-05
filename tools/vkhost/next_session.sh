#!/bin/sh
# Second-session VRAM check: a game unloaded and the next one loaded into
# the same core instance (no dlclose in between) must start from blank VRAM.
#
# Session 1 fills all of VRAM with red every frame. Session 2 draws nothing.
# Both run with the full-VRAM debug view, so every frame session 2 hands
# back must be black: any lit pixel there is the previous game's VRAM
# carried into the new renderer.
#
# Usage, from the repo root after a `make HAVE_HW=1` and `make -C tools/vkhost`:
#     tools/vkhost/next_session.sh mednafen_psx_hw_libretro.so
set -e
core="$1"; frames="${2:-30}"
here="$(cd "$(dirname "$0")" && pwd)"
case "$core" in /*) ;; *) core="$PWD/$core" ;; esac
work="$(mktemp -d)"
mkdir -p /tmp/vkhost_sys
[ -f /tmp/vkhost_sys/scph5501.bin ] || head -c 524288 /dev/zero > /tmp/vkhost_sys/scph5501.bin
python3 - "$work" <<'PY'
import struct, sys

def exe(path, gp0_per_frame):
    code = []
    def lui(rt, i): code.append(0x3C000000 | rt << 16 | (i & 0xffff))
    def ori(rt, rs, i): code.append(0x34000000 | rs << 21 | rt << 16 | (i & 0xffff))
    def sw(rt, off, rs): code.append(0xAC000000 | rs << 21 | rt << 16 | (off & 0xffff))
    def w(val, port):
        lui(9, val >> 16); ori(9, 9, val & 0xffff); sw(9, port, 8)
    lui(8, 0x1F80)
    # GP1: reset, display on, start (0,0), ranges, 320x240
    for c in (0x00000000, 0x03000000, 0x05000000, 0x06C60260, 0x07040010, 0x08000001):
        w(c, 0x1814)
    loop = len(code)
    for c in gp0_per_frame:
        w(c, 0x1810)
    code.extend((0x240A4000, 0x254AFFFF, 0x1540FFFE, 0))     # delay
    code.append(0x08000000 | (((0x80010000 + loop * 4) >> 2) & 0x3ffffff))
    code.append(0)
    body = b''.join(struct.pack('<I', c) for c in code)
    body += b'\0' * ((-len(body)) % 0x800)
    hdr = bytearray(0x800)
    hdr[0:8] = b'PS-X EXE'
    struct.pack_into('<I', hdr, 0x10, 0x80010000)
    struct.pack_into('<I', hdr, 0x18, 0x80010000)
    struct.pack_into('<I', hdr, 0x1c, len(body))
    struct.pack_into('<I', hdr, 0x30, 0x801ffff0)
    open(path, 'wb').write(bytes(hdr) + body)

# GP0(02h) fill: all of VRAM, red, as four 512x256 quarters (a fill is at
# most 1008 wide)
fill = []
for y in (0, 256):
    for x in (0, 512):
        fill += [0x020000FF, (y << 16) | x, (256 << 16) | 512]
exe(sys.argv[1] + '/red.exe', fill)
exe(sys.argv[1] + '/blank.exe', [])
PY

VKHOST_VARS="beetle_psx_hw_display_vram=enabled${VKHOST_VARS:+;$VKHOST_VARS}" \
VKHOST_NEXT_CONTENT="$work/blank.exe" \
   "$here/vkhost" "$core" "$work/red.exe" - "$frames" "$work/out"

python3 - "$work/out" <<'PY'
import glob, os, sys

def lit_pixels(path):
    with open(path, 'rb') as f:
        data = f.read()
    # P6 header: magic, width height, maxval, one whitespace byte
    parts = data.split(b'\n', 3)
    pix = parts[3]
    n = 0
    for i in range(0, len(pix) - 2, 3):
        if pix[i] or pix[i + 1] or pix[i + 2]:
            n += 1
    return n

out = sys.argv[1]
first = sorted(glob.glob(os.path.join(out, 'frame_*.ppm')))
second = sorted(glob.glob(os.path.join(out, 'next', 'frame_*.ppm')))
if not first or not second:
    sys.exit('next_session: missing frame dumps')
if lit_pixels(first[-1]) == 0:
    sys.exit('next_session: session 1 never showed its filled VRAM - probe broken')
bad = [(p, lit_pixels(p)) for p in second]
bad = [b for b in bad if b[1]]
for p, n in bad:
    print('next_session: %s carries %d lit pixels from session 1' % (p, n))
if bad:
    sys.exit(1)
print('next_session: OK (%d frames of session 2 blank)' % len(second))
PY
rm -rf "$work"
