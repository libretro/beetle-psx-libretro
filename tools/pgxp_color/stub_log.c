/* The harnesses link PGXP without libretro.c, which owns log_cb. NULL
 * here; pgxp_gpu.c null-checks before calling, so the instrumentation
 * line is simply inert offline. */
#include <libretro.h>
retro_log_printf_t log_cb = NULL;
