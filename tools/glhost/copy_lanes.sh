#!/bin/sh
# VRAM-to-VRAM copy lanes for the hardware renderers. Content is
# gen_copy_exe.py: overlapping, wrapping and masked GP0(80h) copies over a
# patterned VRAM, read back whole through GP0(C0h) with the software
# framebuffer off, so the dump is what the renderer itself produced.
#
#   model    independent Python model of the hardware copy order
#   sw       software renderer, which must match the model
#   gl*      OpenGL at 1x, 2x, 4x, 32 bpp, without glCopyImageSubData, and
#            with an HDR (fp16) target at 1x and 2x
#   vk*      Vulkan at 1x, 2x, 2x with 4x MSAA, and 32 bpp
#
# Every lane must equal the model bit for bit over all of VRAM. HDR keeps
# drawn colors at full precision, so its model rounds them to 5 bits
# (round(c * 31 / 255)) where hardware truncates (c >> 3).
#
# Usage (from the repository root, after a HAVE_HW=1 build):
#   sh tools/glhost/copy_lanes.sh [core.so]
set -e
cd "$(dirname "$0")/../.."
CORE="${1:-./mednafen_psx_hw_libretro.so}"
case "$CORE" in /*) ;; *) CORE="$PWD/$CORE" ;; esac
OUT=/tmp/glhost_copy_lanes
SYS=/tmp/vkhost_sys
P=beetle_psx_hw
FRAMES=${FRAMES:-120}
RAM="100000:100000"

make -s -C tools/glhost glhost
make -s -C tools/vkhost vkhost
rm -rf "$OUT"
mkdir -p "$OUT" "$SYS"
[ -f "$SYS/scph5501.bin" ] || head -c 524288 /dev/zero > "$SYS/scph5501.bin"
python3 tools/glhost/gen_copy_exe.py "$OUT/copy.exe"

gl()
{
   # $1 name, $2 GLHOST_VARS, $3 GLHOST_HIDE_PROCS
   case "$1" in *hdr*) export HOST_HDR_SCRGB=1 ;; *) unset HOST_HDR_SCRGB ;; esac
   GLHOST_VARS="$2" GLHOST_HIDE_PROCS="$3" GLHOST_RAM_OUT="$OUT/$1.bin:$RAM" \
      tools/glhost/glhost "$CORE" "$OUT/copy.exe" - "$FRAMES" "$OUT/$1" \
      2> "$OUT/$1.log" || { cat "$OUT/$1.log"; exit 1; }
}
vk()
{
   # $1 name, $2 VKHOST_VARS, $3 "novalidation" to run without the layer
   [ "$3" = novalidation ] && export VKHOST_NO_VALIDATION=1
   VKHOST_VARS="${P}_renderer=hardware_vk;$2" VKHOST_RAM_OUT="$OUT/$1.bin:$RAM" \
      tools/vkhost/vkhost "$CORE" "$OUT/copy.exe" - "$FRAMES" "$OUT/$1" \
      > "$OUT/$1.log" 2>&1 || { cat "$OUT/$1.log"; exit 1; }
   unset VKHOST_NO_VALIDATION
}

OFF="${P}_renderer_software_fb=disabled"
gl sw "${P}_renderer=software"
gl gl1x "$OFF"
gl gl2x "$OFF;${P}_internal_resolution=2x"
gl gl4x "$OFF;${P}_internal_resolution=4x"
gl gl32 "$OFF;${P}_depth=32bpp"
gl glhdr1x "$OFF;${P}_color_format=64bit_scrgb"
gl glhdr2x "$OFF;${P}_color_format=64bit_scrgb;${P}_internal_resolution=2x"
gl glnocopy "$OFF;${P}_internal_resolution=2x" \
   glCopyImageSubData,glCopyImageSubDataARB,glCopyImageSubDataEXT,glCopyImageSubDataOES,glCopyImageSubDataNV
vk vk1x "$OFF"
vk vk2x "$OFF;${P}_internal_resolution=2x"
# MSAA with the software framebuffer off already reports render pass
# sample-count mismatches before this lane; only its pixels are checked.
vk vkmsaa "$OFF;${P}_internal_resolution=2x;${P}_msaa=4x" novalidation
vk vk32 "$OFF;${P}_internal_resolution=2x;${P}_depth=32bpp"

python3 - "$OUT" sw gl1x gl2x gl4x gl32 glhdr1x glhdr2x glnocopy vk1x vk2x vkmsaa vk32 <<'EOF'
import re, struct, sys
out = sys.argv[1]
src = open('tools/glhost/gen_copy_exe.py').read()
copies = eval(re.search(r'COPIES = (\[.*?\n\])', src, re.S).group(1))
quads = eval(re.search(r'QUADS = (\[.*?\n\])', src, re.S).group(1))
W, H = 1024, 512
def model(hdr):
    v = [((i ^ (i >> 5) ^ (i >> 11)) & 0xffff) for i in range(W * H)]
    for e6, rgb, x0, y0, x1, y1 in quads:
        q = [(int(c * 31 / 255 + 0.5) if hdr else c >> 3)
             for c in ((rgb >> s) & 255 for s in (0, 8, 16))]
        c = q[0] | (q[1] << 5) | (q[2] << 10)
        for y in range(y0, y1):
            for x in range(x0, x1):
                v[y * W + x] = c | (0x8000 if e6 & 1 else 0)
    # Hardware order, as mednafen's Command_FBCopy: rows top to bottom, each
    # row in 128-pixel chunks read whole before any of the chunk is written.
    for e6, sx, sy, dx, dy, w, h in copies:
        and_, or_ = (0x8000 if e6 & 2 else 0), (0x8000 if e6 & 1 else 0)
        for y in range(h):
            for x0 in range(0, w, 128):
                n = min(128, w - x0)
                buf = [v[((sy + y) & 511) * W + ((sx + x0 + c) & 1023)] for c in range(n)]
                for c in range(n):
                    d = ((dy + y) & 511) * W + ((dx + x0 + c) & 1023)
                    if not (v[d] & and_):
                        v[d] = buf[c] | or_
    return v
models = {False: model(False), True: model(True)}
fail = 0
for name in sys.argv[2:]:
    v = models['hdr' in name]
    d = open('%s/%s.bin' % (out, name), 'rb').read()
    got = struct.unpack('<%dH' % (len(d) // 2), d)
    bad = [i for i in range(W * H) if got[i] != v[i]]
    fail |= bool(bad)
    where = ''
    if bad:
        hits = []
        for k, (e6, sx, sy, dx, dy, w, h) in enumerate(copies):
            n = sum(1 for i in bad if ((i // W - dy) & 511) < h and ((i % W - dx) & 1023) < w)
            if n: hits.append('#%d:%d' % (k, n))
        where = ' (copies %s)' % ' '.join(hits) if hits else ' (outside copies)'
    print('%-9s pixels differing from the model: %6d%s  %s'
          % (name, len(bad), where, 'FAIL' if bad else 'OK'))
sys.exit(1 if fail else 0)
EOF
