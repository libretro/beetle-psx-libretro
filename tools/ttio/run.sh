#!/bin/sh
# Builds and runs tt_io_channel_test under ThreadSanitizer, then under
# AddressSanitizer + UBSan with leak detection.
set -e
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"
SRC="tools/ttio/tt_io_channel_test.c rhi/tt_io_channel.c \
     libretro-common/rthreads/rthreads.c \
     libretro-common/rthreads/retro_eventcount.c \
     libretro-common/features/features_cpu.c"
FLAGS="-std=gnu11 -O1 -g -Wall -DTT_IO_CHANNEL_TEST -DHAVE_THREADS -D__LIBRETRO__ -Ilibretro-common/include"
$CC $FLAGS -fsanitize=thread -o tools/ttio/tt_io_channel_test_tsan $SRC -lpthread
TSAN_OPTIONS=halt_on_error=1 tools/ttio/tt_io_channel_test_tsan "${1:-20000}"
$CC $FLAGS -fsanitize=address,undefined -fno-sanitize-recover=undefined -o tools/ttio/tt_io_channel_test_asan $SRC -lpthread
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 tools/ttio/tt_io_channel_test_asan "${1:-20000}"
rm -f tools/ttio/tt_io_channel_test_tsan tools/ttio/tt_io_channel_test_asan
