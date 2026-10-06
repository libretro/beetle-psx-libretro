#!/bin/sh
# Lightrec threaded-compiler regression lane.
#
# Builds the software core three ways and runs the self-modifying EXE from
# gen_exe.py through each, checking the program's running sum against the
# host's own arithmetic (see lightrec_host.c):
#
#   1. ThreadSanitizer, three workers, a 256 KiB code buffer: every
#      compile overflows the buffer within seconds, so the flush request,
#      the queue cancel, the chunk pool and the deferred frees are all
#      exercised under TSan.
#   2. AddressSanitizer + UBSan, three workers, same tiny buffer, in
#      execute, run_interpreter and disabled modes.
#   3. The single-core shape: no worker thread, the second request for a
#      block compiles it inline on the emulation thread.
#   4. A plain THREADED_RECOMPILER=0 build, execute mode: the
#      single-threaded install path must stay bit-identical.
#
# usage: tools/lightrec/run.sh [frames]
set -e
cd "$(dirname "$0")/../.."
FRAMES="${1:-300}"
CC="${CC:-cc}"
HOST=tools/lightrec/lightrec_host
EXE=/tmp/lightrec_lane.exe
GPUEXE=/tmp/lightrec_lane_gpu.exe
SYS=/tmp/lrhost_sys
# Iterations the two programs complete in 300 frames. They are emulated
# time, so they do not depend on the host; a change in GPU or CPU timing
# visible to the program moves them and fails the lane.
EXE_ITER=61827
GPU_ITER=24999

mkdir -p "$SYS"
[ -f "$SYS/scph5501.bin" ] || head -c 524288 /dev/zero > "$SYS/scph5501.bin"
python3 tools/lightrec/gen_exe.py "$EXE"
python3 tools/lightrec/gen_gpu_exe.py "$GPUEXE"

lane_build() {
	make clean >/dev/null 2>&1 || true
	make -j"$(nproc)" "$@" >/dev/null
}

lane_run() {
	ASAN_OPTIONS=detect_leaks=1:fast_unwind_on_malloc=0 \
	TSAN_OPTIONS=halt_on_error=1 \
	"$HOST" ./mednafen_psx_libretro.so "$EXE" "$FRAMES" "$1" "$2"
}

lane_run_gpu() {
	ASAN_OPTIONS=detect_leaks=1:fast_unwind_on_malloc=0 \
	TSAN_OPTIONS=halt_on_error=1 \
	"$HOST" ./mednafen_psx_libretro.so "$GPUEXE" "$FRAMES" "$1" "$2" nosum
}

echo "== lane 1: TSan, 3 workers, 256 KiB code buffer"
$CC -O1 -g -fsanitize=thread -Ilibretro-common/include -o "$HOST" tools/lightrec/lightrec_host.c -ldl
lane_build DEBUG=1 SANITIZER=thread \
	EXTRA_FLAGS="-DLIGHTREC_TEST_WORKERS=3 -DLIGHTREC_CODEBUFFER_SIZE=262144"
lane_run execute "$([ "$FRAMES" = 300 ] && echo $EXE_ITER)"

echo "== lane 2: ASan+UBSan, 3 workers, 256 KiB code buffer"
# lightning writes x86 immediates at whatever byte the code stream is at,
# so the alignment check is off for the core; everything else stays on.
$CC -O1 -g -fsanitize=address,undefined -Ilibretro-common/include -o "$HOST" tools/lightrec/lightrec_host.c -ldl
lane_build DEBUG=1 SANITIZER=address,undefined \
	EXTRA_FLAGS="-fno-sanitize=alignment -DLIGHTREC_TEST_WORKERS=3 -DLIGHTREC_CODEBUFFER_SIZE=262144"
lane_run execute "$([ "$FRAMES" = 300 ] && echo $EXE_ITER)"
lane_run run_interpreter "$([ "$FRAMES" = 300 ] && echo $EXE_ITER)"
lane_run disabled
lane_run_gpu execute "$([ "$FRAMES" = 300 ] && echo $GPU_ITER)"
lane_run_gpu disabled

echo "== lane 2b: ASan+UBSan, single-core shape (no worker, inline compile)"
lane_build DEBUG=1 SANITIZER=address,undefined \
	EXTRA_FLAGS="-fno-sanitize=alignment -DLIGHTREC_TEST_WORKERS=0 -DLIGHTREC_CODEBUFFER_SIZE=262144"
lane_run execute

echo "== lane 3: non-threaded recompiler"
$CC -O1 -g -Ilibretro-common/include -o "$HOST" tools/lightrec/lightrec_host.c -ldl
lane_build THREADED_RECOMPILER=0
lane_run execute "$([ "$FRAMES" = 300 ] && echo $EXE_ITER)"
lane_run_gpu execute "$([ "$FRAMES" = 300 ] && echo $GPU_ITER)"

rm -f "$HOST"
make clean >/dev/null 2>&1 || true
echo "== lightrec lane OK"
