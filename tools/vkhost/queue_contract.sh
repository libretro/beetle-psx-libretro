#!/bin/sh
# Runs the core's Vulkan renderer under vkhost with the queue contract
# enforced, on content that needs nothing copyrighted: a blank firmware
# image and a program that loops in place. The renderer still starts,
# submits and presents every frame, which is all the check needs.
#
# usage: queue_contract.sh <core.so> [frames]
#
# Fails (vkhost exits non-zero) on any validation error, and on any use
# of the frontend's queue made without lock_queue held.
set -e
core="$1"; frames="${2:-120}"
here="$(cd "$(dirname "$0")" && pwd)"
work="$(mktemp -d)"
mkdir -p /tmp/vkhost_sys
[ -f /tmp/vkhost_sys/scph5501.bin ] || head -c 524288 /dev/zero > /tmp/vkhost_sys/scph5501.bin
python3 - "$work/loop.exe" <<'PY'
import struct, sys
hdr = bytearray(0x800)
hdr[0:8] = b'PS-X EXE'
struct.pack_into('<I', hdr, 0x10, 0x80010000)   # pc0
struct.pack_into('<I', hdr, 0x18, 0x80010000)   # text address
struct.pack_into('<I', hdr, 0x1c, 0x800)        # text size
struct.pack_into('<I', hdr, 0x30, 0x801ffff0)   # stack
code = bytearray(0x800)
code[0:8] = struct.pack('<II', 0x08004000, 0)   # j 0x80010000 ; nop
open(sys.argv[1], 'wb').write(bytes(hdr) + bytes(code))
PY
VKHOST_QUEUE_THREAD=1 "$here/vkhost" "$core" "$work/loop.exe" - "$frames" "$work/out"
