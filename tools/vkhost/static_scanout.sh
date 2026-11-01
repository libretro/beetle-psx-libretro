#!/bin/sh
# Static-screen scanout lanes for the Vulkan renderer, run headless through
# vkhost (lavapipe works). Content is gen_copy_exe.py, a still picture,
# with and without gp1loop (display start and mode sent again, unchanged,
# every frame, as many games do):
#   - the resends change nothing: every frame matches the run without them,
#     with plain output and through the composite cable;
#   - the composite carrier still advances every field on the still
#     picture, as it does on the OpenGL renderer;
#   - with plain output, the resends cost no compute work.
#
# Usage (from the repository root, after a HAVE_HW=1 build):
#   sh tools/vkhost/static_scanout.sh [core.so]
set -e
cd "$(dirname "$0")/../.."
CORE="${1:-./mednafen_psx_hw_libretro.so}"
case "$CORE" in /*) ;; *) CORE="$PWD/$CORE" ;; esac
OUT=/tmp/vk_static_scanout
SYS=/tmp/vkhost_sys
P=beetle_psx_hw
FRAMES=200

make -s -C tools/vkhost vkhost
rm -rf "$OUT"
mkdir -p "$OUT" "$SYS"
[ -f "$SYS/scph5501.bin" ] || head -c 524288 /dev/zero > "$SYS/scph5501.bin"
python3 tools/glhost/gen_copy_exe.py "$OUT/still.exe"
python3 tools/glhost/gen_copy_exe.py "$OUT/resend.exe" gp1loop

run()
{
   # $1 name, $2 content, $3 VKHOST_VARS
   VKHOST_DUMP_INTERVAL=1 VKHOST_VARS="${P}_renderer=hardware_vk;$3" \
      tools/vkhost/vkhost "$CORE" "$OUT/$2.exe" - "$FRAMES" "$OUT/$1" \
      > "$OUT/$1.log" 2>&1 || { tail -20 "$OUT/$1.log"; exit 1; }
}
run plain_still still ""
run plain_resend resend ""
run comp_still still "${P}_video_cable=composite"
run comp_resend resend "${P}_video_cable=composite"

python3 - "$OUT" "$FRAMES" <<'EOF'
import os, re, sys
out, frames = sys.argv[1], int(sys.argv[2])
def f(run, i): return open('%s/%s/frame_%04d.ppm' % (out, run, i), 'rb').read()
def groups(run):
    return int(re.search(r'(\d+) compute workgroups', open('%s/%s.log' % (out, run)).read()).group(1))
ok = True
for cable in ('plain', 'comp'):
    bad = [i for i in range(1, frames + 1) if f(cable + '_still', i) != f(cable + '_resend', i)]
    print('%-5s resends: %d of %d frames differ' % (cable, len(bad), frames))
    ok &= not bad
# The picture is still from frame 150 on; the carrier must keep moving.
moving = sum(1 for i in range(151, frames + 1) if f('comp_still', i) != f('comp_still', i - 1))
print('comp  still picture: %d of %d frames change' % (moving, frames - 150))
ok &= moving == frames - 150
gs, gr = groups('plain_still'), groups('plain_resend')
print('plain compute workgroups, second half: %d still, %d with resends' % (gs, gr))
ok &= gr == gs
print('static_scanout: %s' % ('PASS' if ok else 'FAIL'))
sys.exit(0 if ok else 1)
EOF
