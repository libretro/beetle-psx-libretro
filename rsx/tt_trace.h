/* Trace tool for diagnosing GL backend correctness gaps with the
 * "Software gl_framebuffer" core option toggled off vs on, or against
 * the Vulkan backend.
 *
 * COMPILE-TIME GATED behind HAVE_TRACE:
 *
 *   - Default build (HAVE_TRACE undefined): the entire facility
 *     compiles to nothing. tt_log() expands to a no-op statement,
 *     tt_trace.c is excluded from the build, and call sites generate
 *     no instructions and no rodata. Zero runtime cost, zero binary
 *     size cost.
 *
 *   - Tracing build (HAVE_TRACE=1 in the make invocation, or
 *     -DHAVE_TRACE in CFLAGS): the call sites become real function
 *     calls. Tracing is then ALSO runtime-gated: tt_enabled() returns
 *     true only if BEETLE_PSX_TRACE is set in the environment, so a
 *     HAVE_TRACE=1 build without the env var still pays only a single
 *     load + branch per call site.
 *
 * Output goes through retro_log_printf at RETRO_LOG_DEBUG. The frontend
 * has to be configured to surface DEBUG-level messages (in RetroArch:
 * Frontend Log Level / Core Log Level).
 *
 * Each event is one line, prefixed with "[tt <seq> f<frame>] " for
 * grep/sort/diff convenience. See tt_trace.c for the format strings of
 * individual events.
 *
 * NOT thread-safe by design - the seq counter is racy. That's fine for
 * a diagnostic facility; if two threads emit events concurrently they
 * may interleave or reuse a sequence number, which doesn't break the
 * primary use case (diffing trace logs from two runs of the same scene). */

#ifndef BEETLE_PSX_TT_TRACE_H
#define BEETLE_PSX_TT_TRACE_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_TRACE

#include <stdint.h>
#include <stdbool.h>

/* Returns true if tracing is enabled (env BEETLE_PSX_TRACE non-empty).
 * First call reads the env var; subsequent calls return cached value. */
bool tt_enabled(void);

/* Bump the frame counter (call from rsx_*_finalize_frame). */
void tt_frame_advance(void);

/* Emit a trace line. Format must NOT include a trailing newline if you
 * want this to mesh with retro_log's own line handling - actually, do
 * include it; retro_log doesn't add one. The function checks
 * tt_enabled() internally so call sites can be unconditional.
 *
 * Use the tt_log() wrapper macro so args are not evaluated when
 * tracing is disabled at runtime. */
void tt_log_raw(const char *fmt, ...);

/* Convenience for one-off startup info: emits regardless of frame
 * counter state, with a "[tt startup]" prefix. */
void tt_log_startup(const char *fmt, ...);

#define tt_log(...) \
   do { if (tt_enabled()) tt_log_raw(__VA_ARGS__); } while (0)

#else /* !HAVE_TRACE */

/* Compile-time off: every call site disappears. The do/while(0) keeps
 * the macro safe in if/else without braces and ensures `tt_log(...);`
 * still requires a semicolon. The (void)0 form is used for functions
 * that aren't macros so they collapse to nothing too. */

#define tt_log(...)         do { } while (0)
#define tt_frame_advance()  ((void)0)
#define tt_log_startup(...) do { } while (0)

#endif /* HAVE_TRACE */

#ifdef __cplusplus
}
#endif

#endif /* BEETLE_PSX_TT_TRACE_H */
