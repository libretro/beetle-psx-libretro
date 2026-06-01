/* See tt_trace.h for the design rationale.
 *
 * This file is only added to the build when HAVE_TRACE is set in the
 * make invocation. The #ifdef guard at the top is a belt-and-braces
 * guard so it stays compilable even if someone forces it into a build
 * without -DHAVE_TRACE for some reason (the resulting object will be
 * empty). */

#include "tt_trace.h"

#ifdef HAVE_TRACE

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include <libretro.h>

/* libretro.c has the global definition (no static); we just reach for
 * it. Startup ordering: retro_set_environment - which fills log_cb -
 * runs before any of our hooks fire. */
extern retro_log_printf_t log_cb;

/* -1 = uninitialised, 0 = disabled, 1 = enabled. */
static int      s_tt_state = -1;
static uint64_t s_tt_seq   = 0;
static uint32_t s_tt_frame = 0;

static void tt_init_once(void)
{
   const char *env;
   if (s_tt_state != -1)
      return;
   env = getenv("BEETLE_PSX_TRACE");
   s_tt_state = (env && env[0] != '\0') ? 1 : 0;
   if (s_tt_state && log_cb)
      log_cb(RETRO_LOG_DEBUG,
            "[tt_trace] enabled via BEETLE_PSX_TRACE\n");
}

bool tt_enabled(void)
{
   if (s_tt_state == -1)
      tt_init_once();
   return s_tt_state == 1;
}

void tt_frame_advance(void)
{
   if (!tt_enabled())
      return;
   s_tt_frame++;
}

static void tt_emit(const char *prefix, const char *fmt, va_list ap)
{
   /* Two-shot: build the user-format part into a stack buffer, then
    * ship prefix + body through log_cb. 512 bytes is generous for the
    * format strings we use (longest is ~100 bytes). */
   char line[512];
   int  n;
   if (!log_cb)
      return;
   n = vsnprintf(line, sizeof(line), fmt, ap);
   if (n < 0)
      return;
   /* vsnprintf may have truncated; that's fine for tracing. */
   log_cb(RETRO_LOG_DEBUG, "%s%s", prefix, line);
}

void tt_log_raw(const char *fmt, ...)
{
   char     prefix[64];
   va_list  ap;
   uint64_t seq;
   if (!tt_enabled())
      return;
   seq = ++s_tt_seq;
   snprintf(prefix, sizeof(prefix), "[tt %llu f%u] ",
         (unsigned long long)seq, (unsigned)s_tt_frame);
   va_start(ap, fmt);
   tt_emit(prefix, fmt, ap);
   va_end(ap);
}

void tt_log_startup(const char *fmt, ...)
{
   va_list ap;
   if (!tt_enabled())
      return;
   va_start(ap, fmt);
   tt_emit("[tt startup] ", fmt, ap);
   va_end(ap);
}

#endif /* HAVE_TRACE */
