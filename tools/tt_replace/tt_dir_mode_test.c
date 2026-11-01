/* Regression test for the HD Texture Folder mode shared by the render
 * thread and the I/O workers (struct tt_dir_mode, rhi/rhi_tt.h).
 *
 * The main thread sets the mode the way the renderer does each frame,
 * changing it now and then; reader threads read it the way texture_root()
 * does for every replacement path. Checks that every read is one of the
 * modes set. Built under ThreadSanitizer by run.sh as well, where a plain
 * int in place of the atomic is reported as a data race.
 *
 * Build and run: tools/tt_replace/run.sh */

#include <stdio.h>

#include <rthreads/rthreads.h>

#include "rhi/rhi_tt.h"

retro_log_printf_t log_cb = NULL;

#define READERS 4
#define FRAMES  20000

static struct tt_dir_mode dir_mode = { RETRO_ATOMIC_INT_INITIALIZER(0) };
static retro_atomic_int_t stop     = RETRO_ATOMIC_INT_INITIALIZER(0);
static retro_atomic_int_t bad      = RETRO_ATOMIC_INT_INITIALIZER(0);

static void reader(void *data)
{
   unsigned long reads = 0;
   (void)data;
   while (!retro_atomic_load_acquire_int(&stop) || reads < 1000)
   {
      const int mode = tt_dir_mode_get(&dir_mode);
      if (mode < 0 || mode > 2)
         retro_atomic_fetch_add_int(&bad, 1);
      reads++;
   }
}

int main(void)
{
   sthread_t *threads[READERS];
   int i, frame;

   for (i = 0; i < READERS; i++)
      if (!(threads[i] = sthread_create(reader, NULL)))
      {
         printf("tt_dir_mode_test: could not start a reader\n");
         return 1;
      }

   /* every frame sets it; every 64th frame it changes */
   for (frame = 0; frame < FRAMES; frame++)
      tt_dir_mode_set(&dir_mode, (frame / 64) % 3);

   retro_atomic_store_release_int(&stop, 1);
   for (i = 0; i < READERS; i++)
      sthread_join(threads[i]);

   if (retro_atomic_load_acquire_int(&bad) != 0
         || tt_dir_mode_get(&dir_mode) != ((FRAMES - 1) / 64) % 3)
   {
      printf("tt_dir_mode_test: FAIL\n");
      return 1;
   }
   printf("tt_dir_mode_test: OK\n");
   return 0;
}
