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
STATEXE=/tmp/lightrec_lane_stat.exe
TIMEREXE=/tmp/lightrec_lane_timer.exe
SYS=/tmp/lrhost_sys
# Iterations the two programs complete in 300 frames. They are emulated
# time, so they do not depend on the host; a change in GPU or CPU timing
# visible to the program moves them and fails the lane.
EXE_ITER=61827
GPU_ITER=24999
# The GPUSTAT program also pins the accumulated status words: they move
# with any change to when the GPU refills its draw-time budget.
STAT_ITER_REC=7152
STAT_SUM_REC=3099459584
STAT_ITER_INT=2836
STAT_SUM_INT=3402792960
# The timer program (timer 0 on the dot clock, timer 1 on hblank, GPUSTAT)
# pins the dot-clock and hblank delivery as well as the timer mode writes
# that wake an idle GPU.
TIMER_ITER_REC=16825
TIMER_SUM_REC=1844657834
TIMER_ITER_INT=6534
TIMER_SUM_INT=1446022084

mkdir -p "$SYS"
[ -f "$SYS/scph5501.bin" ] || head -c 524288 /dev/zero > "$SYS/scph5501.bin"
python3 tools/lightrec/gen_exe.py "$EXE"
python3 tools/lightrec/gen_gpu_exe.py "$GPUEXE"
python3 tools/lightrec/gen_stat_exe.py "$STATEXE"
python3 tools/lightrec/gen_timer_exe.py "$TIMEREXE"

lane_build() {
	make clean >/dev/null 2>&1 || true
	make -j"$(nproc)" "$@" >/dev/null
}

lane_run() {
	ASAN_OPTIONS=detect_leaks=1:fast_unwind_on_malloc=0 \
	TSAN_OPTIONS=halt_on_error=1 \
	"$HOST" ./mednafen_psx_libretro.so "$EXE" "$FRAMES" "$1" "$2"
}

lane_run_stat() {
	ASAN_OPTIONS=detect_leaks=1:fast_unwind_on_malloc=0 \
	TSAN_OPTIONS=halt_on_error=1 \
	"$HOST" ./mednafen_psx_libretro.so "$STATEXE" 300 "$1" "$2" "sum=$3"
}

lane_run_timer() {
	ASAN_OPTIONS=detect_leaks=1:fast_unwind_on_malloc=0 \
	TSAN_OPTIONS=halt_on_error=1 \
	"$HOST" ./mednafen_psx_libretro.so "$TIMEREXE" 300 "$1" "$2" "sum=$3"
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
lane_run_stat execute $STAT_ITER_REC $STAT_SUM_REC
lane_run_stat disabled $STAT_ITER_INT $STAT_SUM_INT
lane_run_timer execute $TIMER_ITER_REC $TIMER_SUM_REC
lane_run_timer disabled $TIMER_ITER_INT $TIMER_SUM_INT

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
