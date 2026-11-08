#!/bin/sh
# Host cache maintenance lanes for the Vulkan renderer, run headless
# through vkhost (lavapipe works) with VKHOST_NONCOHERENT, which reports
# every host-visible memory type as non-coherent, as on GPUs whose host
# memory needs explicit flushes and invalidates, and counts the bytes
# those calls cover. Content is gen_copy_exe.py smallread: 250 passes of a
# quad drawn and an 8x8 block uploaded inside a 63x32 rect at an odd x,
# each pass reading back only that rect through GP0(C0h).
#   - the read rect matches a model of VRAM after pass 250 exactly, at 1x
#     and 2x internal resolution, with the software framebuffer on and off;
#   - a readback invalidates only what it read: every readback after the
#     first full-VRAM one stays within a few non-coherent atoms of the
#     rect's 4 KiB (the staging buffer is 1 MiB);
#   - uploads and vertex data flush only what was written, not whole
#     staging blocks and rings.
#
# Usage (from the repository root, after a HAVE_HW=1 build):
#   sh tools/vkhost/host_caches.sh [core.so]
set -e
cd "$(dirname "$0")/../.."
CORE="${1:-./mednafen_psx_hw_libretro.so}"
case "$CORE" in /*) ;; *) CORE="$PWD/$CORE" ;; esac
OUT=/tmp/vk_host_caches
SYS=/tmp/vkhost_sys
P=beetle_psx_hw
FRAMES=120

make -s -C tools/vkhost vkhost
rm -rf "$OUT"
mkdir -p "$OUT" "$SYS"
[ -f "$SYS/scph5501.bin" ] || head -c 524288 /dev/zero > "$SYS/scph5501.bin"
python3 tools/glhost/gen_copy_exe.py "$OUT/small.exe" smallread

run()
{
   # $1 name, $2 VKHOST_VARS
   VKHOST_NONCOHERENT=1 VKHOST_RAM_OUT="$OUT/$1.bin:100000:1000" \
   VKHOST_VARS="${P}_renderer=hardware_vk;$2" \
      tools/vkhost/vkhost "$CORE" "$OUT/small.exe" - "$FRAMES" "$OUT/$1" \
      > "$OUT/$1.log" 2>&1 || { tail -20 "$OUT/$1.log"; exit 1; }
}
run sw1 "${P}_renderer_software_fb=enabled"
run hw1 "${P}_renderer_software_fb=disabled"
run hw2 "${P}_renderer_software_fb=disabled;${P}_internal_resolution=2x"

python3 - "$OUT" <<'EOF'
import re, struct, sys
out = sys.argv[1]
RX, RY, RW, RH, PASSES = 97, 96, 63, 32, 250
def pattern(i): return (i ^ (i >> 5) ^ (i >> 11)) & 0xffff
q = PASSES & 31
model = []
for y in range(RY, RY + RH):
    for x in range(RX, RX + RW):
        if 140 <= x < 148 and 104 <= y < 112: v = PASSES | 0x4000
        elif 100 <= x < 116 and 100 <= y < 116: v = q | (q << 5)
        else: v = pattern(y * 1024 + x)
        model.append(v)
ok = True
for run in ('sw1', 'hw1', 'hw2'):
    got = struct.unpack('<%dH' % (RW * RH), open('%s/%s.bin' % (out, run), 'rb').read()[:RW * RH * 2])
    bad = sum(1 for a, b in zip(got, model) if a != b)
    log = open('%s/%s.log' % (out, run)).read()
    m = re.search(r'host caches: (\d+) bytes invalidated in (\d+) calls, (\d+) bytes flushed in (\d+) calls', log)
    inv, invc, fl, flc = (int(g) for g in m.groups())
    print('%s: %d of %d words differ from the model; invalidated %d bytes in %d calls, flushed %d bytes in %d calls'
          % (run, bad, len(model), inv, invc, fl, flc))
    ok &= bad == 0
    if run != 'sw1':
        # One whole-VRAM read at start-up (2 MiB); the rest one rect each.
        ok &= invc > PASSES
        per = (inv - 2 * 1024 * 1024) / float(invc - 1)
        print('%s: %.0f bytes invalidated per rect readback (limit 8192)' % (run, per))
        ok &= per <= 8192
    # Each pass writes 128 bytes of upload and a few hundred of vertex and
    # uniform data; a flush of whole blocks or of the ring is many KiB.
    per = fl / float(PASSES)
    print('%s: %.0f bytes flushed per pass (limit 16384)' % (run, per))
    ok &= per <= 16384
print('host_caches: %s' % ('PASS' if ok else 'FAIL'))
sys.exit(0 if ok else 1)
EOF
