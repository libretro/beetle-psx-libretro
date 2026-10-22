#!/bin/sh
# Display color lanes for the Vulkan renderer, run headless through vkhost
# (lavapipe works). Content is tools/glhost/gen_color_exe.py. With the RGB
# analog cable at 2x, supersampled scanout must present the white fill at
# the same level as the plain scaled scanout, in HDR and in SDR.
#
# Usage (from the repository root, after a HAVE_HW=1 build):
#   sh tools/vkhost/color_lanes.sh [core.so]
set -e
cd "$(dirname "$0")/../.."
CORE="${1:-./mednafen_psx_hw_libretro.so}"
OUT=/tmp/vkhost_color_lanes
SYS=/tmp/vkhost_sys
P=beetle_psx_hw
FRAMES=120

make -s -C tools/vkhost vkhost
mkdir -p "$OUT" "$SYS"
[ -f "$SYS/scph5501.bin" ] || head -c 524288 /dev/zero > "$SYS/scph5501.bin"
python3 tools/glhost/gen_color_exe.py "$OUT/color.exe"

run()
{
   # $1 name, $2 color format, $3 super sampling
   VKHOST_DUMP_RAW=1 \
   VKHOST_VARS="${P}_color_format=$2;${P}_internal_resolution=2x;${P}_video_cable=rgb;${P}_super_sampling=$3" \
      tools/vkhost/vkhost "$CORE" "$OUT/color.exe" - "$FRAMES" "$OUT/$1" \
      > "$OUT/$1.log" 2>&1 || { cat "$OUT/$1.log"; exit 1; }
}

run hdr 30bit_hdr disabled
run hdr_ssaa 30bit_hdr enabled
run sdr 24bit disabled
run sdr_ssaa 24bit enabled

python3 - "$OUT" "$FRAMES" <<'EOF'
import re, struct, sys
out, frames = sys.argv[1], int(sys.argv[2])
def white_level(name):
    """Mean of each channel over a white-fill area away from the triangle,
    decoded from the raw scanout by the VkFormat vkhost logs."""
    frame = '%s/%s/frame_%04d.ppm' % (out, name, frames)
    log = open('%s/%s.log' % (out, name)).read()
    fmt = int(re.findall(r'dumped \S+frame_%04d\.ppm \(\d+x\d+ fmt (\d+)\)' % frames, log)[-1])
    w, h = map(int, open(frame, 'rb').read().split(b'\n', 3)[1].split())
    raw = open(frame + '.raw', 'rb').read()
    acc = [0.0, 0.0, 0.0]; n = 0
    for y in range(h * 200 // 240, h * 230 // 240):
        for x in range(w * 232 // 320, w * 250 // 320):
            i = y * w + x
            if fmt == 64:    # A2B10G10R10_UNORM_PACK32
                v = struct.unpack_from('<I', raw, i * 4)[0]
                c = (v & 1023, (v >> 10) & 1023, (v >> 20) & 1023)
            elif fmt == 97:  # R16G16B16A16_SFLOAT
                c = struct.unpack_from('<3e', raw, i * 8)
            elif fmt in (37, 44):  # R8G8B8A8 / B8G8R8A8 UNORM
                c = struct.unpack_from('<3B', raw, i * 4)
            else:
                raise SystemExit('%s: unhandled VkFormat %d' % (name, fmt))
            for k in range(3): acc[k] += c[k]
            n += 1
    return [a / n for a in acc]
fail = 0
for ref, test in (('hdr', 'hdr_ssaa'), ('sdr', 'sdr_ssaa')):
    a, b = white_level(ref), white_level(test)
    diff = max(abs(x - y) for x, y in zip(a, b))
    ok = diff <= 0.002 * max(max(a), 1e-6)
    fail |= not ok
    print('%-8s white %s  %-8s white %s  %s' % (ref, [round(v, 3) for v in a],
          test, [round(v, 3) for v in b], 'OK' if ok else 'FAIL'))
sys.exit(1 if fail else 0)
EOF
