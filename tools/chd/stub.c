/* Link support for tools/chd/chd_read_test.c.
 *
 * The CD-ROM backends reach for the frontend log callback and the
 * selected-disc index, both owned by libretro.c; these stand in for them
 * so the harness links the disc readers without the rest of the core. */
#include <stdio.h>
#include <stdarg.h>

static void stub_log(int level, const char *fmt, ...)
{
   va_list ap;
   if (level < 2) /* RETRO_LOG_WARN and up */
      return;
   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   va_end(ap);
}

void (*log_cb)(int level, const char *fmt, ...) = stub_log;

int CD_SelectedDisc = 0;
