/* The Display Transform's per-curve options are shown only while their
 * curve is selected.
 *
 * Loads the core as a frontend would (retro_set_environment only - no
 * content is needed), records every SET_CORE_OPTIONS_DISPLAY call, and
 * drives the update-display callback the core registers with each value of
 * the Display Transform option the core itself declares:
 *
 *   - the ACES 2.0 options appear only under ACES 2.0 (Full Output
 *     Transform), AgX's only under AgX, GT's only under GT (Uchimura),
 *     Filmic's only under Filmic (Sobotka), and every other curve shows
 *     none of them;
 *   - the callback reports a change exactly when one happened;
 *   - every option it names is a real option of the core, and every curve
 *     it names a real value of the Display Transform.
 *
 * usage: dt_visibility <mednafen_psx_hw_libretro.so> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include "libretro.h"

#define P "beetle_psx_hw_"

static const struct
{
   const char *key;
   const char *curve;
} expect[] = {
   { P "hdr_aces2_surround",     "aces2full" },
   { P "hdr_aces2_gamut",        "aces2full" },
   { P "hdr_agx_look",           "agx" },
   { P "hdr_gt_toe",             "gt" },
   { P "hdr_gt_linear",          "gt" },
   { P "hdr_filmiclog_contrast", "filmiclog" },
   { P "hdr_filmiclog_range",    "filmiclog" }
};
#define NEXPECT (sizeof(expect) / sizeof(expect[0]))

static const struct retro_core_option_v2_definition *defs;
static retro_core_options_update_display_callback_t update_display;
static const char *transform = "reinhard";
static int  visible[NEXPECT];
static int  display_calls;
static int  fails;

static void chk(int cond, const char *what, const char *a, const char *b)
{
   if (!cond)
   {
      printf("  FAIL: %s (%s, %s)\n", what, a ? a : "-", b ? b : "-");
      fails++;
   }
}

static const struct retro_core_option_v2_definition *find(const char *key)
{
   const struct retro_core_option_v2_definition *d;
   for (d = defs; d && d->key; d++)
      if (!strcmp(d->key, key))
         return d;
   return NULL;
}

static bool env_cb(unsigned cmd, void *data)
{
   switch (cmd)
   {
      case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
         *(unsigned *)data = 2;
         return true;
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
         defs = ((const struct retro_core_options_v2_intl *)data)->us->definitions;
         return true;
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
         defs = ((const struct retro_core_options_v2 *)data)->definitions;
         return true;
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
         update_display = data
            ? ((const struct retro_core_options_update_display_callback *)data)->callback
            : NULL;
         return true;
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
      {
         const struct retro_core_option_display *d =
            (const struct retro_core_option_display *)data;
         unsigned i;
         display_calls++;
         for (i = 0; i < NEXPECT; i++)
            if (!strcmp(d->key, expect[i].key))
               visible[i] = d->visible;
         return true;
      }
      case RETRO_ENVIRONMENT_GET_VARIABLE:
      {
         struct retro_variable *v = (struct retro_variable *)data;
         if (!strcmp(v->key, P "hdr_display_transform"))
         {
            v->value = transform;
            return true;
         }
         v->value = NULL;
         return false;
      }
      default:
         return false;
   }
}

int main(int argc, char **argv)
{
   void (*set_env)(retro_environment_t);
   const struct retro_core_option_v2_definition *dt;
   const struct retro_core_option_value *val;
   void    *core;
   unsigned i;
   int      curves = 0;

   if (argc < 2)
   {
      fprintf(stderr, "usage: %s core.so\n", argv[0]);
      return 2;
   }
   core = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
   if (!core)
   {
      fprintf(stderr, "%s\n", dlerror());
      return 2;
   }
   *(void **)&set_env = dlsym(core, "retro_set_environment");
   if (!set_env)
      return 2;

   for (i = 0; i < NEXPECT; i++)
      visible[i] = 1;
   set_env(env_cb);

   chk(defs != NULL, "core options set", NULL, NULL);
   chk(update_display != NULL, "update-display callback registered", NULL, NULL);
   if (!defs || !update_display)
      return 1;
   dt = find(P "hdr_display_transform");
   chk(dt != NULL, "Display Transform option exists", NULL, NULL);
   if (!dt)
      return 1;
   for (i = 0; i < NEXPECT; i++)
   {
      int found = 0;
      chk(find(expect[i].key) != NULL, "option exists", expect[i].key, NULL);
      for (val = dt->values; val->value; val++)
         if (!strcmp(val->value, expect[i].curve))
            found = 1;
      chk(found, "curve is a Display Transform value", expect[i].curve, NULL);
   }

   /* Every curve the core offers, twice over so the second pass starts
    * from each other curve's state, then the default again. */
   {
      int pass;
      for (pass = 0; pass < 2; pass++)
         for (val = dt->values; val->value; val++)
         {
            int before[NEXPECT], changed = 0, want_change = 0;
            bool reported;
            for (i = 0; i < NEXPECT; i++)
               before[i] = visible[i];
            transform = val->value;
            reported  = update_display();
            for (i = 0; i < NEXPECT; i++)
            {
               int want = !strcmp(val->value, expect[i].curve);
               chk(visible[i] == want, want ? "shown under its curve"
                     : "hidden under another curve", expect[i].key, val->value);
               if (visible[i] != before[i])
                  changed = 1;
               if (want != before[i])
                  want_change = 1;
            }
            chk(reported == (bool)want_change, "callback reports the change",
                  val->value, reported ? "reported" : "not reported");
            chk(changed == want_change, "only changed options touched",
                  val->value, NULL);
            chk(!update_display(), "a second call changes nothing",
                  val->value, NULL);
            curves += pass == 0;
         }
   }

   printf("%d Display Transform curves checked, %d display calls\n",
         curves, display_calls);
   printf(fails ? "dt_visibility: %d failures\n" : "dt_visibility: all checks passed\n",
         fails);
   return fails ? 1 : 0;
}
