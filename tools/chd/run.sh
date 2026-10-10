#!/bin/sh
# CHD read regression test.
#
#   sh tools/chd/run.sh
#
# Writes a synthetic two-track image (tools/chd/make_fixture.py), compresses
# it with each CD codec family chdman offers -- zstd (cdzs), LZMA (cdlz),
# zlib (cdzl), FLAC (cdfl) -- and as a parent/child/grandchild chain, then
# checks every CHD against the CUE it came from with chd_read_test, built
# under AddressSanitizer + UBSan with leak detection. Each image is read
# twice: through the file, and with image_memcache.
#
# Needs chdman (MAME tools) and python3. Fails rather than skips when either
# is missing: a CHD check that quietly does nothing is how a codec stops
# working without anyone noticing.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"

command -v chdman >/dev/null || { echo "chdman not found" >&2; exit 1; }
command -v python3 >/dev/null || { echo "python3 not found" >&2; exit 1; }

CC=${CC:-cc}
CF=$(tools/harness_cflags.sh)
LC=libretro-common
CD=mednafen/cdrom
WORK=${TMPDIR:-/tmp}/chd_read_test.$$
mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT

SRC="tools/chd/chd_read_test.c tools/chd/stub.c
     mednafen/general.c mednafen/error.c mednafen/cdstream.c
     $CD/CDAccess.c $CD/CDAccess_CCD.c $CD/CDAccess_Image.c
     $CD/CDAccess_PBP.c $CD/CDAccess_CHD.c $CD/audioreader.c
     $CD/cdaccess_track.c $CD/CDUtility.c $CD/galois.c
     $CD/l-ec.c $CD/lec.c $CD/recover-raw.c $CD/edc_crc32.c
     $LC/formats/chd/rchd.c $LC/encodings/encoding_huffman.c
     $LC/encodings/encoding_rzstd.c $LC/encodings/encoding_deflate.c
     $LC/encodings/encoding_crc32.c $LC/encodings/encoding_utf.c
     $LC/formats/7z/r7z_lzma.c $LC/formats/flac/rflac.c
     $LC/formats/vorbis/rvorbis.c
     $LC/streams/file_stream.c $LC/streams/trans_stream.c
     $LC/streams/trans_stream_deflate.c $LC/streams/trans_stream_pipe.c
     $LC/streams/trans_stream_rzstd.c
     $LC/vfs/vfs_implementation.c $LC/vfs/vfs_implementation_cdrom.c
     $LC/cdrom/cdrom.c $LC/file/file_path.c $LC/file/file_path_io.c
     $LC/file/retro_dirent.c $LC/memmap/memalign.c $LC/memmap/memmap.c
     $LC/lists/string_list.c $LC/lists/dir_list.c
     $LC/formats/data_transfer.c $LC/hash/lrc_hash.c
     $LC/compat/compat_strl.c $LC/compat/fopen_utf8.c
     $LC/compat/compat_posix_string.c $LC/compat/compat_strcasestr.c
     $LC/string/stdstring.c $LC/string/rstrtod.c $LC/time/rtime.c
     $LC/rthreads/rthreads.c $LC/rthreads/retro_eventcount.c
     $LC/features/features_cpu.c $LC/queues/retro_spsc.c
     $LC/queues/retro_waitable_spsc.c"

$CC -O1 -g $CF -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -I$LC/include -Imednafen -I. -o "$WORK/chd_read_test" $SRC -lm -lpthread

python3 tools/chd/make_fixture.py "$WORK/base"
python3 tools/chd/make_fixture.py "$WORK/child" --variant 1
python3 tools/chd/make_fixture.py "$WORK/grandchild" --variant 2

for c in cdzs cdlz cdzl cdfl; do
   case $c in
      cdzs) name="CD Zstandard" ;;
      cdlz) name="CD LZMA" ;;
      cdzl) name="CD Deflate" ;;
      cdfl) name="CD FLAC" ;;
   esac
   chdman createcd -f -c "$c" -i "$WORK/base.cue" -o "$WORK/$c.chd" \
      >/dev/null 2>&1
   # A codec that fell back to storing hunks uncompressed would leave
   # nothing for this test to decode, so its row in the hunk table has to
   # be there.
   chdman info -v -i "$WORK/$c.chd" | grep -q "%  $name" \
      || { echo "$c.chd holds no $name hunks" >&2; exit 1; }
done

# Parent chain: the child stores the hunks it changed and copies the rest
# from the parent, so most of its sectors are decoded out of parent.chd.
# chdman cannot index a parent that has a parent of its own, so the
# grandchild stores every hunk itself; it checks that a three-image chain
# is found and opened, not hunk resolution two levels down.
mkdir -p "$WORK/chain"
chdman createcd -f -c cdzs -i "$WORK/base.cue" -o "$WORK/chain/parent.chd" \
   >/dev/null 2>&1
chdman createcd -f -c cdzs,cdlz -i "$WORK/child.cue" \
   -op "$WORK/chain/parent.chd" -o "$WORK/chain/child.chd" >/dev/null 2>&1
chdman info -v -i "$WORK/chain/child.chd" | grep -q "%  Copy from parent" \
   || { echo "child.chd copies nothing from its parent" >&2; exit 1; }
chdman createcd -f -c cdzs,cdfl -i "$WORK/grandchild.cue" \
   -op "$WORK/chain/child.chd" -o "$WORK/chain/grandchild.chd" >/dev/null 2>&1

export ASAN_OPTIONS=detect_leaks=1:abort_on_error=1
fail=0
for c in cdzs cdlz cdzl cdfl; do
   for m in file memcache; do
      "$WORK/chd_read_test" "$WORK/base.cue" "$WORK/$c.chd" "$m" | tail -1 \
         | grep -q PASS || { echo "FAIL: $c ($m)"; fail=1; }
   done
done
for img in child grandchild; do
   for m in file memcache; do
      "$WORK/chd_read_test" "$WORK/$img.cue" "$WORK/chain/$img.chd" "$m" \
         | tail -1 | grep -q PASS || { echo "FAIL: $img ($m)"; fail=1; }
   done
done

if [ $fail = 0 ]; then
   echo "chd_read_test: all images PASS"
else
   exit 1
fi
