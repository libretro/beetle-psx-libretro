/* Regression test for the Replace Textures session latch (rhi/rhi_tt.h).
 *
 * Drives the latch the way rhi_tt.c does: texture_tracker_set_config takes
 * the menu value every frame, the in-game toggle flips the tracker and the
 * latch, a renderer rebuild makes a new tracker that starts with
 * replacement off, and unloading the game resets the latch. Checks that the
 * last choice made by menu or toggle holds across rebuilds, that an
 * unchanged menu value never undoes the toggle, that a changed one does,
 * and that the next game starts from the menu value.
 *
 * Build and run: tools/tt_replace/run.sh */

#include <stdio.h>

#include "rhi/rhi_tt.h"

retro_log_printf_t log_cb = NULL;

static struct tt_replace_latch latch = { -1, -1 };
static bool tracker_on;
static unsigned failures;

/* texture_tracker_set_config */
static void set_config(bool menu)
{
   bool on = tt_replace_latch_menu(&latch, menu);
   if (tracker_on != on)
      tracker_on = on;
}

/* the in-game toggle (texture_tracker_endFrame) */
static void toggle(void)
{
   tracker_on = !tracker_on;
   tt_replace_latch_toggle(&latch, tracker_on);
}

/* renderer rebuild: a new tracker starts off, then sees the options */
static void rebuild(bool menu)
{
   tracker_on = false;
   set_config(menu);
}

static void expect(const char *what, bool want)
{
   if (tracker_on != want)
   {
      fprintf(stderr, "FAIL %s: replacement %s, want %s\n", what,
            tracker_on ? "on" : "off", want ? "on" : "off");
      failures++;
   }
}

int main(void)
{
   int frame;

   /* menu on: a new game's first tracker turns replacement on */
   tracker_on = false;
   set_config(true);
   expect("menu on, first frame", true);

   /* toggle off; the unchanged menu value must not undo it */
   toggle();
   for (frame = 0; frame < 3; frame++)
      set_config(true);
   expect("toggled off, menu unchanged", false);

   /* the toggle survives a rebuild */
   rebuild(true);
   expect("toggled off, after rebuild", false);
   toggle();
   expect("toggled back on", true);
   rebuild(true);
   expect("toggled on, after rebuild", true);

   /* a changed menu value overrides the toggle, and survives a rebuild */
   set_config(false);
   expect("menu changed to off", false);
   rebuild(false);
   expect("menu off, after rebuild", false);
   toggle();
   rebuild(false);
   expect("menu off, toggled on, after rebuild", true);
   set_config(true);
   set_config(false);
   expect("menu changed on then off", false);

   /* unload: the next game starts from the menu value */
   set_config(true);
   toggle();
   expect("toggled off before unload", false);
   tt_replace_latch_reset(&latch);
   rebuild(true);
   expect("next game, menu on", true);
   tt_replace_latch_reset(&latch);
   rebuild(false);
   expect("next game, menu off", false);

   if (failures)
   {
      fprintf(stderr, "tt_replace_latch_test: %u failure(s)\n", failures);
      return 1;
   }
   printf("tt_replace_latch_test: OK\n");
   return 0;
}
