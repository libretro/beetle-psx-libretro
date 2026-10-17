#!/bin/sh
# HDR display-metadata lanes.
#
# 1. hdr_display_test: the PQ encode and the Rec.2020 -> panel matrix, under
#    ASan and UBSan. Portable C89; builds anywhere a C compiler does.
#
# 2. vkhost (Vulkan, lavapipe) and glhost (GL, llvmpipe) end to end, when a
#    hardware core is built: a probe EXE fills four bars (black, green, grey,
#    white) with NTSC 1953 source primaries, whose green lies outside a P3
#    panel, and the host presents a series of displays through
#    GET_DISPLAY_METADATA / GET_HDR_METADATA:
#      base      unknown panel, no black            reference frame
#      panel     P3 primaries                       green brought inside P3,
#                                                   grey and white untouched
#      black     frontend black 0.05 nits           BT.2390 toe on every bar
#      caps      panel black 0.05 nits only         same frame as "black"
#      hdronly   P3 panel, GET_HDR_METADATA only    same frame as "base"
#      peak      no frontend peak, panel peak 600   peak taken from the panel
#      regrade   passthrough off                    24-bit fallback at load
#      analog    composite cable, unknown panel     reference for the next
#      analog_panel  composite, P3, black 0.05      green inside P3, black
#                                                   bar on the toe (Vulkan)
#
# KEEP=<dir> keeps the logs and frames there instead of a temporary dir.
#
# Usage, from the repo root after `make HAVE_HW=1`:
#     sh tools/hdr_display/run.sh [mednafen_psx_hw_libretro.so]
set -e
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
core="${1:-mednafen_psx_hw_libretro.so}"
case "$core" in /*) ;; *) core="$PWD/$core" ;; esac
work="${KEEP:-$(mktemp -d)}"
trap '[ -n "$KEEP" ] || rm -rf "$work"' EXIT

echo "== hdr_display_test (ASan+UBSan)"
${CC:-cc} -std=c89 -pedantic -Wall -Wno-long-long -O1 -g \
   -fsanitize=address,undefined -fno-sanitize-recover=all \
   -I"$root/libretro-common/include" \
   -o "$work/hdr_display_test" \
   "$here/hdr_display_test.c" "$root/rhi/hdr_display.c" -lm
"$work/hdr_display_test"

if [ ! -f "$core" ]; then
   echo "== end-to-end lanes skipped: no $core (build with make HAVE_HW=1)"
   exit 0
fi

python3 - "$work/bars.exe" <<'PY'
import struct, sys
code = []
def lui(rt, i): code.append(0x3C000000 | rt << 16 | (i & 0xffff))
def ori(rt, rs, i): code.append(0x34000000 | rs << 21 | rt << 16 | (i & 0xffff))
def sw(rt, off, rs): code.append(0xAC000000 | rs << 21 | rt << 16 | (off & 0xffff))
def w(val, port):
    lui(9, val >> 16); ori(9, 9, val & 0xffff); sw(9, port, 8)
lui(8, 0x1F80)
# GP1: reset, display on, start (0,0), ranges, 320x240, 24-bit off
for c in (0x00000000, 0x03000000, 0x05000000, 0x06C60260, 0x07040010, 0x08000001):
    w(c, 0x1814)
loop = len(code)
# GP0(02h) fills, colour 0xBBGGRR: black, green, grey, white bars, 80 wide
for i, col in enumerate((0x000000, 0x00FF00, 0x808080, 0xFFFFFF)):
    for c in (0x02000000 | col, i * 80, (240 << 16) | 80):
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
open(sys.argv[1], 'wb').write(bytes(hdr) + body)
PY

P3=6800,3200,2650,6900,1500,600
FRAMES=60
mkdir -p /tmp/vkhost_sys
[ -f /tmp/vkhost_sys/scph5501.bin ] || head -c 524288 /dev/zero > /tmp/vkhost_sys/scph5501.bin

# run <host> <scenario> [VAR=value ...]: runs the host under the given
# environment, keeping its log and its last frame. CABLE picks the video
# cable, so the analog resolve's own copy of the encode is covered too.
run() {
   host="$1"; name="$2"; shift 2
   mkdir -p "$work/$host/$name"
   opts="beetle_psx_hw_src_primaries=ntsc1953;beetle_psx_hw_video_cable=${CABLE:-off}"
   if [ "$host" = vk ]; then
      env VKHOST_VARS="$opts" VKHOST_DUMP_RAW=1 \
         VKHOST_DUMP_INTERVAL=$FRAMES "$@" \
         "$root/tools/vkhost/vkhost" "$core" "$work/bars.exe" - $FRAMES \
         "$work/$host/$name" > "$work/$host/$name/log" 2>&1
   else
      env GLHOST_VARS="beetle_psx_hw_color_format=30bit_hdr;$opts" \
         "$@" \
         "$root/tools/glhost/glhost" "$core" "$work/bars.exe" - $FRAMES \
         "$work/$host/$name" > "$work/$host/$name/log" 2>&1
   fi
}

scenarios() {
   host="$1"
   run "$host" base
   run "$host" panel   HOST_HDR_PANEL=$P3
   run "$host" black   HOST_HDR_BLACK=0.05
   run "$host" caps    HOST_HDR_PANEL_BLACK=0.05
   run "$host" hdronly HOST_HDR_PANEL=$P3 HOST_HDR_NO_DISPLAY=1
   run "$host" peak    HOST_HDR_MAX=0 HOST_HDR_PANEL_PEAK=600
   run "$host" regrade HOST_HDR_PASSTHROUGH=0
   # Vulkan only: under glhost the GL analog chain presents a black frame
   # with any cable, in 24-bit as well, so there is nothing to measure there.
   if [ "$host" = vk ]; then
      CABLE=composite run "$host" analog
      CABLE=composite run "$host" analog_panel HOST_HDR_PANEL=$P3 HOST_HDR_BLACK=0.05
   fi
}

lanes=""
echo "== vkhost (Vulkan)"
if make -s -C "$root/tools/vkhost" >/dev/null 2>&1; then
   scenarios vk; lanes="$lanes vk"
else
   echo "   skipped: vkhost does not build here"
fi
echo "== glhost (GL)"
if make -s -C "$root/tools/glhost" >/dev/null 2>&1; then
   scenarios gl; lanes="$lanes gl"
else
   echo "   skipped: glhost does not build here"
fi

python3 - "$work" $lanes <<'PY'
import glob, os, re, struct, sys
import numpy as np

work, lanes = sys.argv[1], sys.argv[2:]
fails = []

def pq_to_nits(e):
    m1, m2 = 2610 / 16384, 2523 / 4096 * 128
    c1, c2, c3 = 3424 / 4096, 2413 / 4096 * 32, 2392 / 4096 * 32
    p = np.power(np.clip(e, 0, 1), 1 / m2)
    return 10000 * np.power(np.maximum(p - c1, 0) / (c2 - c3 * p), 1 / m1)

def pq(nits):
    m1, m2 = 2610 / 16384, 2523 / 4096 * 128
    c1, c2, c3 = 3424 / 4096, 2413 / 4096 * 32, 2392 / 4096 * 32
    y = np.power(nits / 10000, m1)
    return np.power((c1 + c2 * y) / (1 + c3 * y), m2)

def rgb_to_xyz(xy, w=(0.3127, 0.3290)):
    p = np.array([[x / y, 1, (1 - x - y) / y] for x, y in xy]).T
    s = np.linalg.solve(p, np.array([w[0] / w[1], 1, (1 - w[0] - w[1]) / w[1]]))
    return p * s

TO_P3 = np.linalg.inv(rgb_to_xyz([(0.680, 0.320), (0.265, 0.690), (0.150, 0.060)])) \
      @ rgb_to_xyz([(0.708, 0.292), (0.170, 0.797), (0.131, 0.046)])

def log(host, name):
    return open(os.path.join(work, host, name, 'log'), errors='replace').read()

def bars(host, name):
    """PQ code values (0..1) at the middle of each bar: black, green, grey, white."""
    d = os.path.join(work, host, name)
    if host == 'vk':
        raws = sorted(glob.glob(os.path.join(d, 'frame_*.ppm.raw')))
        m = re.findall(r'dumped \S+ \((\d+)x(\d+) fmt (\d+)\)', log(host, name))
        if not raws or not m:
            fails.append('%s/%s: no frame dumped' % (host, name)); return None
        w, h, fmt = map(int, m[-1])
        data = open(raws[-1], 'rb').read()
        out = []
        for x in (40, 120, 200, 280):
            i = (h // 2) * w + x
            if fmt == 64:      # A2B10G10R10_UNORM_PACK32
                v = struct.unpack_from('<I', data, i * 4)[0]
                out.append(np.array([v & 1023, (v >> 10) & 1023, (v >> 20) & 1023]) / 1023)
            elif fmt == 97:    # R16G16B16A16_SFLOAT
                out.append(np.frombuffer(data, '<f2', 3, i * 8).astype(float))
            else:
                fails.append('%s/%s: scanout format %d is not HDR10' % (host, name, fmt))
                return None
        return np.array(out), 1.5 / 1023
    ppms = sorted(glob.glob(os.path.join(d, 'frame_*.ppm')))
    if not ppms:
        fails.append('%s/%s: no frame dumped' % (host, name)); return None
    data = open(ppms[-1], 'rb').read()
    _, wh, _, pix = data.split(b'\n', 3)
    w, h = map(int, wh.split())
    return np.array([[pix[((h // 2) * w + x) * 3 + c] for c in range(3)]
                     for x in (40, 120, 200, 280)]) / 255, 1.5 / 255

def check(ok, msg):
    if not ok:
        fails.append(msg)

for host in lanes:
    base = bars(host, 'base')
    if base is None:
        continue
    base, tol = base
    green2020 = pq_to_nits(base[1])
    check((TO_P3 @ green2020).min() < -0.02 * green2020.max(),
          '%s: probe green is not outside P3, so the panel lane proves nothing' % host)
    check(base[0].max() == 0, '%s: black bar is not PQ 0 with no black reported' % host)

    r = bars(host, 'panel')
    if r is not None:
        panel, _ = r
        g = TO_P3 @ pq_to_nits(panel[1])
        check(g.min() >= -0.01 * g.max(), '%s/panel: green still outside P3 (%s)' % (host, g))
        check(np.abs(panel[1] - base[1]).max() > tol, '%s/panel: green unchanged' % host)
        check(np.abs(panel[[0, 2, 3]] - base[[0, 2, 3]]).max() <= tol,
              '%s/panel: in-gamut bars changed' % host)

    for name in ('black', 'caps'):
        r = bars(host, name)
        if r is None:
            continue
        got, _ = r
        b = pq(0.05)
        want = base + b * (1 - base) ** 4
        check(np.abs(got - want).max() <= tol,
              '%s/%s: bars %s, expected the BT.2390 toe %s' % (host, name, got.round(4), want.round(4)))

    r = bars(host, 'hdronly')
    if r is not None:
        check(np.abs(r[0] - base).max() == 0,
              '%s/hdronly: frame differs from base without GET_DISPLAY_METADATA' % host)

    check('Display peak: 600 nits' in log(host, 'peak'),
          '%s/peak: panel peak not used when the frontend reports none' % host)

    r = r2 = None
    if host == 'vk':
        r, r2 = bars(host, 'analog'), bars(host, 'analog_panel')
    if r is not None and r2 is not None:
        a, ap = r[0], r2[0]
        g = TO_P3 @ pq_to_nits(a[1])
        check(g.min() < -0.02 * g.max(),
              '%s/analog: composite green is not outside P3, so the lane proves nothing' % host)
        g = TO_P3 @ pq_to_nits(ap[1])
        check(g.min() >= -0.01 * g.max(), '%s/analog_panel: green still outside P3 (%s)' % (host, g))
        b = pq(0.05)
        want = a[0] + b * (1 - a[0]) ** 4
        check(np.abs(ap[0] - want).max() <= tol,
              '%s/analog_panel: black bar %s, expected the toe %s' % (host, ap[0].round(4), want.round(4)))

    lg = log(host, 'regrade')
    fmts = re.findall(r'pixel format (\d+)', lg)
    check('re-grade' in lg and fmts and fmts[-1] == '1',
          '%s/regrade: no 24-bit fallback when HDR10 input would be re-graded' % host)
    print('   %s: checked' % host)

for f in fails:
    print('FAIL: ' + f)
if fails:
    sys.exit(1)
print('hdr_display: OK (%s)' % (' '.join(lanes) or 'unit test only'))
PY
