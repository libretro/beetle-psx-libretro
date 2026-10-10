#!/bin/sh
# Builds and runs mc_async_test under ThreadSanitizer, then under
# AddressSanitizer + UBSan with leak detection.
set -e
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"
SRC="tools/mcasync/mc_async_test.c mc_async.c \
     libretro-common/rthreads/rthreads.c \
     libretro-common/rthreads/retro_eventcount.c \
     libretro-common/features/features_cpu.c"
FLAGS="-std=gnu11 -O1 -g -Wall -DMC_ASYNC_TEST -DHAVE_THREADS -D__LIBRETRO__ -Ilibretro-common/include"
$CC $FLAGS -fsanitize=thread -o tools/mcasync/mc_async_test_tsan $SRC -lpthread
TSAN_OPTIONS=halt_on_error=1 tools/mcasync/mc_async_test_tsan "${1:-20000}"
$CC $FLAGS -fsanitize=address,undefined -fno-sanitize-recover=undefined -o tools/mcasync/mc_async_test_asan $SRC -lpthread
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 tools/mcasync/mc_async_test_asan "${1:-20000}"
rm -f tools/mcasync/mc_async_test_tsan tools/mcasync/mc_async_test_asan
