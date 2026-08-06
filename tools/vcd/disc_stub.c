/* Link support for tools/vcd/vcd_disc.c.
 *
 * The CD-ROM stack reaches for a few things the core proper owns: the
 * frontend log callback, the selected-disc index, and the deflate backends
 * the CHD/PBP readers use. This harness opens a plain CUE and never touches
 * the compressed paths, so stubs satisfy the link without pulling in
 * libretro.c and the rest of the emulator. */
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>

/* retro_log_printf_t, spelled out so this does not need libretro.h. */
static void stub_log(int level, const char *fmt, ...)
{
   va_list ap;
   (void)level;
   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   va_end(ap);
}

void (*log_cb)(int level, const char *fmt, ...) = stub_log;

int CD_SelectedDisc = 0;

/* Only reached for CHD and PBP images. */
void *deflate_deflate_backend = NULL;
void *deflate_inflate_backend = NULL;
