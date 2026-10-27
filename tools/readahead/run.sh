#!/bin/sh
# Mapped disc image read-ahead regression test.
#
#   sh tools/readahead/run.sh            Linux, ASan + UBSan, leaks on
#   MINGW=1 sh tools/readahead/run.sh    also mingw-w64 under Wine, in
#                                        each RETRO_VFS_PREFETCH mode, and
#                                        with the prefetch made slow: no
#                                        read waits on it
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
LC=libretro-common
WORK=${TMPDIR:-/tmp}/mapped_readahead.$$
mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT

SRC="tools/readahead/mapped_readahead_test.c mednafen/cdstream.c
     $LC/streams/file_stream.c $LC/vfs/vfs_implementation.c
     $LC/formats/data_transfer.c $LC/file/file_path.c
     $LC/file/file_path_io.c $LC/compat/compat_strl.c
     $LC/compat/fopen_utf8.c $LC/encodings/encoding_utf.c
     $LC/string/stdstring.c $LC/string/rstrtod.c $LC/time/rtime.c
     $LC/rthreads/rthreads.c $LC/rthreads/retro_eventcount.c
     $LC/features/features_cpu.c $LC/memmap/memmap.c $LC/memmap/memalign.c"
FLAGS="-O1 -g -Wall -DHAVE_THREADS -I$LC/include -Imednafen -I."
WRAP="-Wl,--wrap=filestream_prefetch"

${CC:-cc} $FLAGS -DHAVE_MMAP -fsanitize=address,undefined \
   -fno-sanitize-recover=undefined -o "$WORK/t" $SRC $WRAP -lpthread
( cd "$WORK" && ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 ./t )

if [ -n "$MINGW" ]; then
   x86_64-w64-mingw32-gcc $FLAGS -o "$WORK/t.exe" $SRC $WRAP -static
   for m in "" touch none; do
      ( cd "$WORK" && RETRO_VFS_PREFETCH=$m WINEDEBUG=-all \
         timeout 120 wine ./t.exe )
   done
   # PrefetchVirtualMemory swapped for a stand-in that sleeps like a cold
   # disk: a reader in order never waits on it
   OT="tools/readahead/offthread_test.c ${SRC#tools/readahead/mapped_readahead_test.c}"
   x86_64-w64-mingw32-gcc $FLAGS -DVFS_WIN32_PREFETCH_HOOK=readahead_test_hook \
      -include tools/readahead/prefetch_hook.h -o "$WORK/ot.exe" $OT $WRAP -static
   ( cd "$WORK" && WINEDEBUG=-all timeout 120 wine ./ot.exe )
fi
