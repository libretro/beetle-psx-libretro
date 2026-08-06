#!/bin/sh
# Build a tools/vcd harness against the core sources.
#
#   sh tools/vcd/build.sh <harness> [extra cc args...]
#
# where <harness> is one of: vcd_probe vcd_pipeline vcd_state
#                            cdstream_map_test vcd_disc
#
# The binary lands in $OUT (default /tmp/<harness>). Pass
# -fsanitize=address,undefined to run under the sanitizers.
#
# Preprocessor flags come from tools/harness_cflags.sh, which reads them back
# out of the core's own build rather than restating them. That is the point
# of this script existing. Every harness here used to carry a hand-written
# -D list, and those lists drifted from the core's: built without
# -DHAVE_MMAP, a harness that opens the same disc images the core opens never
# takes the file mapping path, so a heap corruption that fired on every
# mapped image was unreachable in test while the coverage looked complete.
# Restoring the real flags also pulls in dependencies the short lists hid --
# the CHD stack under -DHAVE_CHD, rthreads under -DHAVE_THREADS -- which is
# the same drift seen from the other side.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"

HARNESS=$1
[ -n "$HARNESS" ] || { sed -n '2,10p' "$0"; exit 2; }
shift

CF=$(tools/harness_cflags.sh)
LC=libretro-common
CD=mednafen/cdrom
INC="-I$LC/include -Imednafen -I. -Ideps/zstd-1.5.7"

# Shared by every harness: threads and time come in under -DHAVE_THREADS.
BASE="$LC/compat/compat_strl.c $LC/compat/fopen_utf8.c
      $LC/compat/compat_posix_string.c $LC/compat/compat_strcasestr.c
      $LC/string/stdstring.c $LC/encodings/encoding_utf.c
      $LC/time/rtime.c $LC/rthreads/rthreads.c $LC/features/features_cpu.c"

MPEG="$LC/formats/mpeg1/rmpeg1_ps.c $LC/formats/mpeg1/rmpeg1_video.c
      $LC/formats/mp3/rmp3.c"

VFS="$LC/streams/file_stream.c $LC/vfs/vfs_implementation.c
     $LC/vfs/vfs_implementation_cdrom.c $LC/cdrom/cdrom.c
     $LC/file/file_path.c $LC/file/file_path_io.c $LC/file/retro_dirent.c
     $LC/memmap/memalign.c $LC/memmap/memmap.c $LC/lists/string_list.c
     $LC/lists/dir_list.c $LC/formats/data_transfer.c"

case "$HARNESS" in
vcd_probe|vcd_pipeline|vcd_state)
   SRC="tools/vcd/$HARNESS.c tools/vcd/state_stub.c
        mednafen/psx/vcd.c mednafen/state.c $MPEG $BASE"
   ;;
cdstream_map_test)
   SRC="tools/vcd/$HARNESS.c tools/vcd/disc_stub.c
        mednafen/cdstream.c $VFS $BASE"
   ;;
vcd_disc)
   SRC="tools/vcd/$HARNESS.c tools/vcd/state_stub.c tools/vcd/disc_stub.c
        mednafen/psx/vcd.c mednafen/state.c mednafen/cdstream.c
        mednafen/general.c mednafen/error.c
        $CD/CDAccess.c $CD/CDAccess_CCD.c $CD/CDAccess_Image.c
        $CD/CDAccess_PBP.c $CD/CDAccess_CHD.c $CD/audioreader.c
        $CD/cdromif.c $CD/cdaccess_track.c $CD/CDUtility.c $CD/galois.c
        $CD/l-ec.c $CD/lec.c $CD/recover-raw.c $CD/edc_crc32.c
        $MPEG $VFS $BASE
        $LC/formats/vorbis/rvorbis.c $LC/formats/flac/rflac.c
        $LC/formats/libchdr/libchdr_bitstream.c
        $LC/formats/libchdr/libchdr_cdrom.c $LC/formats/libchdr/libchdr_chd.c
        $LC/formats/libchdr/libchdr_flac.c
        $LC/formats/libchdr/libchdr_flac_codec.c
        $LC/formats/libchdr/libchdr_huffman.c
        $LC/formats/libchdr/libchdr_lzma.c $LC/formats/7z/r7z_lzma.c
        $LC/formats/libchdr/libchdr_zlib.c $LC/formats/libchdr/libchdr_zstd.c
        deps/zstd-1.5.7/zstddeclib.c
        $LC/streams/chd_stream.c $LC/streams/memory_stream.c
        $LC/streams/interface_stream.c $LC/streams/trans_stream.c
        $LC/streams/trans_stream_pipe.c $LC/streams/rzip_stream.c
        $LC/encodings/encoding_crc32.c $LC/encodings/encoding_deflate.c
        $LC/hash/lrc_hash.c"
   ;;
*)
   echo "unknown harness: $HARNESS" >&2; exit 2 ;;
esac

exec ${CC:-gcc} -O1 -g $CF "$@" $INC -o "${OUT:-/tmp/$HARNESS}" $SRC -lm -lpthread
