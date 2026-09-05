#include <stdio.h>
#include <string.h>

#include "../../input.h"
#include "../../input_compatibility.h"
#include "../../libretro_game_database.h"

#define PSX_GAME(redump_id, serial, source_serial, category, version, title)
#define PSX_GAME_COMPAT(redump_id, serial, source_serial, category, version, \
      title, settings) \
   { serial, title, settings },
static const struct beetle_game_database_entry
source_compatibility_entries[] = {
#include "../../database/psx_games.inc"
};
#undef PSX_GAME_COMPAT
#undef PSX_GAME

static int failures;

static void check_database_structure(void)
{
   const uint32_t known_settings =
      PSX_COMPAT_PGXP_MEM_CPU |
      PSX_COMPAT_PGXP_PCT_ON |
      PSX_COMPAT_PGXP_PCT_OFF |
      PSX_COMPAT_PGXP_CULLING_ON |
      PSX_COMPAT_FBWRITE_FIFO_DELAY |
      PSX_COMPAT_PGXP_CACHE_ON |
      PSX_COMPAT_PGXP_CACHE_OFF |
      PSX_COMPAT_CD_SPEED_MASK |
      BEETLE_DB_PORT1_SUPPORT_MASK;
   const size_t expected_count = sizeof(source_compatibility_entries) /
      sizeof(source_compatibility_entries[0]);
   size_t i;

   if (beetle_game_database_count() != expected_count)
   {
      fprintf(stderr,
            "FAIL runtime database has %u entries; source defines %u\n",
            (unsigned)beetle_game_database_count(),
            (unsigned)expected_count);
      failures++;
   }

   for (i = 0; i < expected_count; i++)
   {
      const struct beetle_game_database_entry *expected =
         &source_compatibility_entries[i];
      const struct beetle_game_database_entry *actual;
      char normalized[BEETLE_DISC_SERIAL_SIZE];
      size_t j;

      memset(normalized, 0, sizeof(normalized));
      if (!expected->settings || (expected->settings & ~known_settings) ||
          ((expected->settings & PSX_COMPAT_PGXP_PCT_ON) &&
           (expected->settings & PSX_COMPAT_PGXP_PCT_OFF)) ||
          ((expected->settings & PSX_COMPAT_PGXP_CACHE_ON) &&
           (expected->settings & PSX_COMPAT_PGXP_CACHE_OFF)))
      {
         fprintf(stderr, "FAIL %-12s invalid settings=%08x\n",
               expected->serial, expected->settings);
         failures++;
      }
      if (!expected->title || !expected->title[0])
      {
         fprintf(stderr, "FAIL %-12s has no title\n", expected->serial);
         failures++;
      }
      if (!beetle_game_database_normalize_serial(expected->serial,
            normalized) || strcmp(normalized, expected->serial))
      {
         fprintf(stderr, "FAIL %-12s is not a canonical serial\n",
               expected->serial);
         failures++;
      }

      actual = beetle_game_database_lookup(expected->serial);
      if (!actual || actual->settings != expected->settings ||
          strcmp(beetle_game_database_title(actual), expected->title))
      {
         fprintf(stderr, "FAIL %-12s runtime entry differs from source\n",
               expected->serial);
         failures++;
      }

      for (j = i + 1; j < expected_count; j++)
         if (!strcmp(expected->serial,
               source_compatibility_entries[j].serial))
         {
            fprintf(stderr, "FAIL %-12s duplicate runtime serial\n",
                  expected->serial);
            failures++;
         }
   }
}

static void check_controller_resolution(const char *name,
      unsigned requested, uint32_t supported, unsigned expected)
{
   unsigned resolved = input_resolve_compatible_controller(
         requested, supported);

   if (resolved != expected)
   {
      fprintf(stderr, "FAIL %-28s resolved=%u expected=%u\n",
            name, resolved, expected);
      failures++;
   }
}

static void check_normalization(const char *source, const char *expected)
{
   char serial[BEETLE_DISC_SERIAL_SIZE];

   memset(serial, 0, sizeof(serial));
   if (!beetle_game_database_normalize_serial(source, serial) ||
       strcmp(serial, expected))
   {
      fprintf(stderr, "FAIL %-12s normalized=%s expected=%s\n",
            source, serial, expected);
      failures++;
   }
}

static void check_invalid_normalization(const char *source)
{
   char serial[BEETLE_DISC_SERIAL_SIZE];

   memset(serial, 0, sizeof(serial));
   if (beetle_game_database_normalize_serial(source, serial))
   {
      fprintf(stderr, "FAIL %-12s unexpectedly normalized=%s\n",
            source, serial);
      failures++;
   }
}

int main(void)
{
   const uint32_t vertex_cache_settings =
      PSX_COMPAT_PGXP_CACHE_ON |
      PSX_COMPAT_PGXP_CACHE_OFF;

   if ((PSX_COMPAT_PGXP_CACHE_ON &
        PSX_COMPAT_PGXP_CACHE_OFF) ||
       (vertex_cache_settings & (PSX_COMPAT_CD_SPEED_MASK |
        BEETLE_DB_PORT1_SUPPORT_MASK)))
   {
      fprintf(stderr, "FAIL PGXP vertex-cache compatibility flags overlap\n");
      failures++;
   }

   if (BEETLE_DB_GET_PORT1_SUPPORT(BEETLE_DB_PORT1_SUPPORT(
         BEETLE_DB_CTRL_DIGITAL | BEETLE_DB_CTRL_DUALSHOCK)) !=
       (BEETLE_DB_CTRL_DIGITAL | BEETLE_DB_CTRL_DUALSHOCK))
   {
      fprintf(stderr, "FAIL controller support set did not round-trip\n");
      failures++;
   }

   check_controller_resolution("supported request",
         RETRO_DEVICE_PS_DUALSHOCK,
         BEETLE_DB_CTRL_DIGITAL | BEETLE_DB_CTRL_DUALSHOCK,
         RETRO_DEVICE_PS_DUALSHOCK);
   check_controller_resolution("first supported fallback",
         RETRO_DEVICE_PS_ANALOG,
         BEETLE_DB_CTRL_DIGITAL | BEETLE_DB_CTRL_DUALSHOCK,
         RETRO_DEVICE_JOYPAD);
   check_controller_resolution("compatibility cleared",
         RETRO_DEVICE_PS_ANALOG, BEETLE_DB_CTRL_NONE,
         RETRO_DEVICE_PS_ANALOG);
   check_controller_resolution("digital controller alias",
         RETRO_DEVICE_PS_CONTROLLER, BEETLE_DB_CTRL_DIGITAL,
         RETRO_DEVICE_PS_CONTROLLER);
   check_controller_resolution("disconnected port",
         RETRO_DEVICE_NONE, BEETLE_DB_CTRL_DUALSHOCK,
         RETRO_DEVICE_NONE);

   check_normalization("ABCD_123.45;1", "ABCD-12345");
   check_normalization("abcd-12345", "ABCD-12345");
   check_normalization("TEST_A12.34;1", "TEST-A1234");
   check_invalid_normalization("TEST");
   check_invalid_normalization("TEST_12X.34");
   check_invalid_normalization("TEST_1A2.34");

   check_database_structure();

   if (beetle_game_database_lookup(NULL) ||
       beetle_game_database_lookup(""))
   {
      fprintf(stderr, "FAIL empty serial unexpectedly found\n");
      failures++;
   }

   if (failures)
   {
      printf("FAIL count %d\n", failures);
      return 1;
   }

   printf("database serials %u\n", (unsigned)beetle_game_database_count());
   printf("FAIL count 0\nPASS\n");
   return 0;
}
