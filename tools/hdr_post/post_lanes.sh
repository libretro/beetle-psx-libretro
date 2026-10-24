#!/bin/sh
# HDR post-effect lanes: bloom, halation, chromatic aberration and the
# scene-linear round trip they run through, on the Vulkan renderer (vkhost,
# lavapipe works) and the OpenGL renderer (glhost, llvmpipe works). Output is
# scRGB, so both hosts dump linear floats and the checks read light, not
# encoded codes. Content is gen_post_exe.py: two reference-white squares on
# black, one at the centre and one near the top-left corner.
#
#   1. Effects off: the black around the squares stays black.
#   2. Bloom: a halo of real light around the centre square, and the
#      square itself is no dimmer.
#   3. Halation: the halo is warm (red well above blue); without it the
#      halo is neutral.
#   4. Convolution bloom: a softer core and a far longer tail than the
#      two bands.
#   5. Chromatic aberration (4x): red and blue split apart at the corner
#      square and not at all at the centre one. The centre and mid-grey
#      squares, where it is zero or uniform, match the effects-off picture -
#      which checks the scene-linear image plus composite reproduces the
#      direct encode, at a value where gamma and linear differ.
#   6. The same holds through the analog cable's resolve and the adaptive
#      smoothing resolve, the other stages that can feed the effects.
#   7. The Display Transform changes tone below paper white, and changing
#      it or the post effects mid-run applies without a restart.
#   8. Compact Intermediates (11/11/10 float scene and bloom images) keeps
#      every pixel within the packed format's precision of the half-float
#      picture, with bloom and aberration on and through the analog cable -
#      and does change it, so the packed format is really in use.
#   9. Every lane agrees between Vulkan and OpenGL - ACES 2.0 (full) among
#      them, read through its baked cube by the end of the run.
#
# Usage (from the repository root, after a HAVE_HW=1 build):
#   sh tools/hdr_post/post_lanes.sh [core.so]
set -e
cd "$(dirname "$0")/../.."
CORE="${1:-./mednafen_psx_hw_libretro.so}"
case "$CORE" in /*) ;; *) CORE="$PWD/$CORE" ;; esac
OUT=/tmp/hdr_post_lanes
SYS=/tmp/vkhost_sys
P=beetle_psx_hw
FRAMES=300

make -s -C tools/vkhost vkhost
make -s -C tools/glhost glhost
rm -rf "$OUT"
mkdir -p "$OUT" "$SYS"
[ -f "$SYS/scph5501.bin" ] || head -c 524288 /dev/zero > "$SYS/scph5501.bin"
python3 tools/hdr_post/gen_post_exe.py "$OUT/post.exe"

run()
{
   # $1 lane name, $2 extra core options, $3 frame:key=value changes mid-run
   HOST_HDR_SCRGB=1 VKHOST_DUMP_RAW=1 VKHOST_VARS_AT="$3" \
   VKHOST_VARS="${P}_renderer=hardware_vk;${P}_color_format=64bit_scrgb;$2" \
      tools/vkhost/vkhost "$CORE" "$OUT/post.exe" - "$FRAMES" "$OUT/$1.vk" \
      > "$OUT/$1.vk.log" 2>&1 || { cat "$OUT/$1.vk.log"; exit 1; }
   if ! grep -q " 0 validation errors" "$OUT/$1.vk.log"; then
      cat "$OUT/$1.vk.log"; echo "$1: Vulkan validation errors" >&2; exit 1
   fi
   HOST_HDR_SCRGB=1 GLHOST_VARS_AT="$3" \
   GLHOST_VARS="${P}_renderer=hardware_gl;${P}_color_format=64bit_scrgb;$2" \
      tools/glhost/glhost "$CORE" "$OUT/post.exe" - "$FRAMES" "$OUT/$1.gl" \
      > "$OUT/$1.gl.log" 2>&1 || { cat "$OUT/$1.gl.log"; exit 1; }
}

run off       ""
run bloom     "${P}_hdr_bloom=standard"
run halation  "${P}_hdr_bloom=standard;${P}_hdr_halation=strong"
run conv      "${P}_hdr_bloom=standard;${P}_hdr_bloom_convolution=enabled"
run off4x     "${P}_internal_resolution=4x"
run ca4x      "${P}_internal_resolution=4x;${P}_hdr_chromatic=heavy"
run analog    "${P}_video_cable=svideo;${P}_hdr_bloom=standard"
run smooth2x  "${P}_internal_resolution=2x;${P}_adaptive_smoothing=enabled;${P}_hdr_bloom=standard"
run aces      "${P}_hdr_display_transform=aces;${P}_hdr_bloom=standard"
run live      "" "270:${P}_hdr_display_transform=aces;270:${P}_hdr_bloom=standard"
FX="${P}_internal_resolution=4x;${P}_hdr_bloom=standard;${P}_hdr_chromatic=heavy"
run full4x    "$FX"
run compact4x "$FX;${P}_hdr_post_compact=enabled"
run analogc   "${P}_video_cable=svideo;${P}_hdr_bloom=standard;${P}_hdr_post_compact=enabled"
run aces2f    "${P}_hdr_display_transform=aces2full;${P}_hdr_bloom=standard"

python3 - "$OUT" "$FRAMES" <<'EOF'
import struct, sys, glob, re
out, frames = sys.argv[1], int(sys.argv[2])
fail = []

def half(h):
    s = -1.0 if h & 0x8000 else 1.0
    e = (h >> 10) & 31
    m = h & 1023
    if e == 0:
        return s * m * 2.0 ** -24
    if e == 31:
        return float('nan')
    return s * (1.0 + m / 1024.0) * 2.0 ** (e - 15)

def load(lane, r):
    """The last frame as rows of (R, G, B) in nits, Rec.709, top-down."""
    log = open('%s/%s.%s.log' % (out, lane, r)).read()
    if 'scRGB' not in log:
        fail.append('%s.%s: scRGB was not engaged' % (lane, r))
    if r == 'vk':
        m = re.findall(r'dumped \S+frame_%04d\.ppm \((\d+)x(\d+) fmt (\d+)\)' % frames, log)
        w, h, fmt = map(int, m[-1])
        if fmt != 97:
            fail.append('%s.vk: scanout format %d, expected R16G16B16A16_SFLOAT' % (lane, fmt))
        raw = open('%s/%s.vk/frame_%04d.ppm.raw' % (out, lane, frames), 'rb').read()
        v = struct.unpack('<%dH' % (w * h * 4), raw[:w * h * 8])
        px = [half(x) * 80.0 for x in v]
        rows = [[tuple(px[(y * w + x) * 4:(y * w + x) * 4 + 3]) for x in range(w)] for y in range(h)]
    else:
        m = re.findall(r'dumped \S+frame_%04d\.ppm \((\d+)x(\d+)\)' % frames, log)
        w, h = map(int, m[-1])
        raw = open('%s/%s.gl/frame_%04d.ppm.raw' % (out, lane, frames), 'rb').read()
        v = struct.unpack('<%df' % (w * h * 4), raw[:w * h * 16])
        rows = [[tuple(x * 80.0 for x in v[((h - 1 - y) * w + x) * 4:((h - 1 - y) * w + x) * 4 + 3])
                 for x in range(w)] for y in range(h)]
    return rows

def pq(nits):
    """10-bit PQ code of a light level, as the HDR10 output encodes it."""
    y = min(max(nits / 10000.0, 0.0), 1.0) ** 0.1593017578125
    return round(((0.8359375 + 18.8515625 * y) / (1.0 + 18.6875 * y)) ** 78.84375 * 1023.0)

def lum(c):
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]

def ring(img, sq, s, d0, d1):
    """Pixels whose Chebyshev distance from square (x, y, size) * s lies in [d0, d1)."""
    x0, y0, n = sq[0] * s, sq[1] * s, sq[2] * s
    acc = []
    for y in range(max(0, y0 - d1), min(len(img), y0 + n + d1)):
        for x in range(max(0, x0 - d1), min(len(img[0]), x0 + n + d1)):
            dx = max(x0 - x, 0, x - (x0 + n - 1))
            dy = max(y0 - y, 0, y - (y0 + n - 1))
            d = max(dx, dy)
            if d0 <= d < d1:
                acc.append(img[y][x])
    return acc

def sides(img, sq, s, d0, d1):
    """The square's own rows, [d0, d1) pixels to its left and right: clear
    of the other squares, which sit above, below and in the corner."""
    x0, y0, n = sq[0] * s, sq[1] * s, sq[2] * s
    acc = []
    for y in range(y0, y0 + n):
        for x in list(range(x0 - d1 + 1, x0 - d0 + 1)) + list(range(x0 + n - 1 + d0, x0 + n - 1 + d1)):
            if 0 <= x < len(img[0]):
                acc.append(img[y][x])
    return acc

def mean(cs, f):
    return sum(f(c) for c in cs) / max(len(cs), 1)

def inside(img, sq, s, margin):
    x0, y0, n = sq[0] * s, sq[1] * s, sq[2] * s
    return [img[y][x] for y in range(y0 + margin, y0 + n - margin)
                      for x in range(x0 + margin, x0 + n - margin)]

def centroid(img, sq, s, ch):
    """Light-weighted x centroid of one channel over the square's rows."""
    x0, y0, n = sq[0] * s, sq[1] * s, sq[2] * s
    tw = tx = 0.0
    for y in range(y0 + 2 * s, y0 + n - 2 * s):
        for x in range(max(0, x0 - 8 * s), x0 + n + 8 * s):
            v = img[y][x][ch]
            tw += v
            tx += v * x
    return tx / tw

CENTRE, CORNER, GREY = (152, 112, 16), (16, 16, 16), (152, 192, 16)
# RGB5 grey 16, displayed as the byte 16 * 8 = 128, through the 2.4
# reference transfer at paper white 200.
GREY_NITS = (128.0 / 255.0) ** 2.4 * 200.0
lanes = ['off', 'bloom', 'halation', 'conv', 'off4x', 'ca4x', 'analog', 'smooth2x',
         'aces', 'live', 'full4x', 'compact4x', 'analogc', 'aces2f']
img = {}
for lane in lanes:
    for r in ('vk', 'gl'):
        img[lane, r] = load(lane, r)

for r in ('vk', 'gl'):
    white = mean(inside(img['off', r], CENTRE, 1, 3), lum)
    if not 150.0 < white < 250.0:
        fail.append('%s: reference white reads %.1f nits' % (r, white))
    near_off = mean(ring(img['off', r], CENTRE, 1, 3, 10), lum)
    near_bloom = mean(ring(img['bloom', r], CENTRE, 1, 3, 10), lum)
    if near_off > 0.05:
        fail.append('%s: effects off, the black around the square is %.3f nits' % (r, near_off))
    if near_bloom < 1.0:
        fail.append('%s: bloom halo is %.3f nits' % (r, near_bloom))
    if mean(inside(img['bloom', r], CENTRE, 1, 3), lum) < white - 0.5:
        fail.append('%s: bloom dimmed the square' % r)
    halo = ring(img['bloom', r], CENTRE, 1, 3, 10)
    warm = ring(img['halation', r], CENTRE, 1, 3, 10)
    rb_plain = mean(halo, lambda c: c[0]) / max(mean(halo, lambda c: c[2]), 1e-6)
    rb_warm = mean(warm, lambda c: c[0]) / max(mean(warm, lambda c: c[2]), 1e-6)
    if not 0.8 < rb_plain < 1.25:
        fail.append('%s: plain bloom halo red/blue %.2f, expected neutral' % (r, rb_plain))
    if rb_warm < 1.5:
        fail.append('%s: halation halo red/blue %.2f, expected warm' % (r, rb_warm))
    # Same energy spread wider: a softer core and a much longer tail.
    core_two = mean(ring(img['bloom', r], CENTRE, 1, 1, 4), lum)
    core_conv = mean(ring(img['conv', r], CENTRE, 1, 1, 4), lum)
    far_two = mean(sides(img['bloom', r], CENTRE, 1, 60, 90), lum)
    far_conv = mean(sides(img['conv', r], CENTRE, 1, 60, 90), lum)
    if core_conv > core_two * 0.9:
        fail.append('%s: convolution core %.3f vs two-band %.3f nits, expected softer'
                    % (r, core_conv, core_two))
    if far_conv < far_two * 1.6 or far_conv < 0.005:
        fail.append('%s: convolution tail %.4f vs two-band %.4f nits, expected longer'
                    % (r, far_conv, far_two))
    split_corner = centroid(img['ca4x', r], CORNER, 4, 0) - centroid(img['ca4x', r], CORNER, 4, 2)
    split_centre = centroid(img['ca4x', r], CENTRE, 4, 0) - centroid(img['ca4x', r], CENTRE, 4, 2)
    split_off = centroid(img['off4x', r], CORNER, 4, 0) - centroid(img['off4x', r], CORNER, 4, 2)
    if abs(split_corner) < 1.0:
        fail.append('%s: aberration splits the corner by %.2f px' % (r, split_corner))
    if abs(split_centre) > 0.05 or abs(split_off) > 0.05:
        fail.append('%s: red/blue split %.3f px at the centre, %.3f px with the effect off'
                    % (r, split_centre, split_off))
    grey = mean(inside(img['off', r], GREY, 1, 3), lum)
    if abs(grey - GREY_NITS) > GREY_NITS * 0.03:
        fail.append('%s: mid grey reads %.2f nits, expected %.2f' % (r, grey, GREY_NITS))
    a = inside(img['ca4x', r], CENTRE, 4, 8) + inside(img['ca4x', r], GREY, 4, 8)
    b = inside(img['off4x', r], CENTRE, 4, 8) + inside(img['off4x', r], GREY, 4, 8)
    worst = max(abs(p[i] - q[i]) / max(q[i], 1.0) for p, q in zip(a, b) for i in range(3))
    if worst > 0.01:
        fail.append('%s: the post composite differs from the direct encode by %.2f%%'
                    % (r, worst * 100))
    # The other display stages that feed the post effects: the analog
    # cable's resolve and the adaptive-smoothing resolve.
    for lane, sc in (('analog', 1), ('smooth2x', 2)):
        if mean(ring(img[lane, r], CENTRE, sc, 3 * sc, 10 * sc), lum) < 1.0:
            fail.append('%s: no bloom halo through the %s display stage' % (r, lane))
        if mean(inside(img[lane, r], CENTRE, sc, 4 * sc), lum) < white * 0.8:
            fail.append('%s: the square is dim through the %s display stage' % (r, lane))
    # A Display Transform changes tone below paper white (ACES lifts mid
    # grey), and options changed mid-run apply: the live lane switches the
    # curve and bloom on at frame 270 and must end on the picture a run
    # started with them shows.
    g_aces = mean(inside(img['aces', r], GREY, 1, 3), lum)
    if g_aces < grey * 1.2:
        fail.append('%s: ACES leaves mid grey at %.2f nits (%.2f without)' % (r, g_aces, grey))
    live = max(abs(p[i] - q[i]) for ra, rb in zip(img['live', r], img['aces', r])
               for p, q in zip(ra, rb) for i in range(3))
    if live > 0.05:
        fail.append('%s: options changed mid-run did not apply (%.2f nits off)' % (r, live))
    # Compact Intermediates: within the packed float's precision, measured
    # as 10-bit PQ output codes - 6-bit mantissas for red and green, 5 for
    # blue, rounded once in the scene and again down the bloom chain - but
    # not identical.
    for lane, ref in (('compact4x', 'full4x'), ('analogc', 'analog')):
        worst = [0, 0, 0]
        changed = 0
        for ra, rb in zip(img[lane, r], img[ref, r]):
            for p, q in zip(ra, rb):
                for i in range(3):
                    d = abs(pq(p[i]) - pq(q[i]))
                    changed += d > 0
                    worst[i] = max(worst[i], d)
        if worst[0] > 4 or worst[1] > 4 or worst[2] > 8:
            fail.append('%s: %s is %d/%d/%d PQ codes off half floats' % ((r, lane) + tuple(worst)))
        if not changed:
            fail.append('%s: %s is identical to half floats, compact format not in use'
                        % (r, lane))
        if lane == 'compact4x':
            compact_worst = worst
    print('%s: white %.1f nits, halo %.2f nits, halation r/b %.2f, conv reach %.3f/%.3f, '
          'corner split %.2f px, compact %d/%d/%d PQ codes' % ((r, white, near_bloom, rb_warm, far_conv,
          far_two, split_corner) + tuple(compact_worst)))

for lane in lanes:
    a, b = img[lane, 'vk'], img[lane, 'gl']
    if len(a) != len(b) or len(a[0]) != len(b[0]):
        fail.append('%s: Vulkan %dx%d, OpenGL %dx%d' % (lane, len(a[0]), len(a), len(b[0]), len(b)))
        continue
    d = sum(abs(lum(p) - lum(q)) for ra, rb in zip(a, b) for p, q in zip(ra, rb))
    d /= len(a) * len(a[0])
    if d > 0.5:
        fail.append('%s: Vulkan and OpenGL differ by %.3f nits on average' % (lane, d))

if fail:
    print('\n'.join('FAIL ' + f for f in fail))
    sys.exit(1)
print('hdr_post: all lanes passed')
EOF
