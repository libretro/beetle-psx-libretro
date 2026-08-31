#include <stdio.h>
#include <string.h>

#include "../../libretro_game_database.h"

#define PSX_GAME(redump_id, serial, source_serial, category, version, title) + 1
#define PSX_GAME_COMPAT(redump_id, serial, source_serial, category, version, \
      title, settings) + 1
enum
{
   game_database_catalog_count = 0
#include "../../database/psx_games.inc"
};
#undef PSX_GAME_COMPAT
#undef PSX_GAME

static int failures;

static const struct beetle_game_database_entry *require_entry(
      const char *serial)
{
   const struct beetle_game_database_entry *entry =
      beetle_game_database_lookup(serial);

   if (!entry)
   {
      fprintf(stderr, "FAIL %-12s missing from game database\n", serial);
      failures++;
   }
   return entry;
}

static void check_settings(const char *serial, uint32_t required,
      uint32_t forbidden, unsigned cd_speedup_max)
{
   const struct beetle_game_database_entry *entry = require_entry(serial);
   unsigned entry_cd_speedup_max;

   if (!entry)
      return;
   entry_cd_speedup_max =
      PSX_COMPAT_GET_MAX_CD_SPEED(entry->settings);
   if ((entry->settings & required) != required ||
       (entry->settings & forbidden) ||
       entry_cd_speedup_max != cd_speedup_max)
   {
      fprintf(stderr,
            "FAIL %-12s settings=%08x required=%08x forbidden=%08x "
            "cap=%u expected=%u\n",
            serial, entry->settings, required, forbidden,
            entry_cd_speedup_max, cd_speedup_max);
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
   static const char *spyro_serials[] = {
      "SCUS-94228", "SCES-01438", "SCPS-10085", "SCPS-10083",
      "SCPS-45395", "SCUS-94425", "SCES-02104", "SCPS-10128",
      "SCUS-94467", "SCES-02835"
   };
   static const char *ridge_racer_type_4_serials[] = {
      "SCPS-45354", "SCPS-45355", "SLPS-01798", "SCPS-45356",
      "SLPS-01800", "SLUS-00797", "SCES-01706", "SLPS-91463",
      "SCPS-46001"
   };
   static const char *gran_turismo_2_serials[] = {
      "SCES-02380", "SCES-12380", "SCPS-10116", "SCPS-10117",
      "SCPS-45457", "SCPS-45458", "SCPS-91326", "SCPS-91327",
      "SCUS-94455", "SCUS-94488", "SCAJ-01001", "SCAJ-01002"
   };
   static const char *myst_serials[] = {
      "SCUS-94602", "SLES-00218", "SLPS-00024", "SLPS-91023",
      "SLPS-91123", "SLPS-02924"
   };
   const uint32_t pct_culling = PSX_COMPAT_PGXP_PCT_ON |
      PSX_COMPAT_PGXP_CULLING_ON;
   const uint32_t spyro = PSX_COMPAT_PGXP_MEM_CPU |
      PSX_COMPAT_PGXP_PCT_OFF;
   const uint32_t pct_enable = PSX_COMPAT_PGXP_PCT_ON;
   const struct beetle_game_database_entry *entry;
   size_t i;

   if ((PSX_COMPAT_PGXP_CACHE_ON &
        PSX_COMPAT_PGXP_CACHE_OFF) ||
       (vertex_cache_settings & PSX_COMPAT_CD_SPEED_MASK))
   {
      fprintf(stderr, "FAIL PGXP vertex-cache compatibility flags overlap\n");
      failures++;
   }

   check_normalization("SCUS_942.28;1", "SCUS-94228");
   check_normalization("sces-01706", "SCES-01706");
   check_normalization("SCUS_944.88;1", "SCUS-94488");
   check_normalization("SCUS_941.8a;1", "SCUS-9418A");
   check_invalid_normalization("SCUS");
   check_invalid_normalization("SCUS_94X.28");

   if (game_database_catalog_count != 11932)
   {
      fprintf(stderr, "FAIL source database has %u entries\n",
            (unsigned)game_database_catalog_count);
      failures++;
   }

   if (beetle_game_database_count() != 39)
   {
      fprintf(stderr, "FAIL runtime database has %u configured serials\n",
            (unsigned)beetle_game_database_count());
      failures++;
   }

   for (i = 0; i < sizeof(spyro_serials) / sizeof(spyro_serials[0]); i++)
      check_settings(spyro_serials[i], spyro, pct_enable, 0);
   for (i = 0; i < sizeof(ridge_racer_type_4_serials) /
         sizeof(ridge_racer_type_4_serials[0]); i++)
      check_settings(ridge_racer_type_4_serials[i], pct_culling,
            PSX_COMPAT_PGXP_MEM_CPU, 0);
   for (i = 0; i < sizeof(gran_turismo_2_serials) /
         sizeof(gran_turismo_2_serials[0]); i++)
      check_settings(gran_turismo_2_serials[i], pct_culling,
            PSX_COMPAT_PGXP_MEM_CPU, 0);
   for (i = 0; i < sizeof(myst_serials) / sizeof(myst_serials[0]); i++)
      check_settings(myst_serials[i], 0,
            0xffffffffu & ~PSX_COMPAT_CD_SPEED_MASK, 3);

   check_settings("SLUS-00765", PSX_COMPAT_FBWRITE_FIFO_DELAY,
         0, 0);
   check_settings("SLES-00979", PSX_COMPAT_FBWRITE_FIFO_DELAY,
         0, 0);

   if (beetle_game_database_lookup("SCUS-94900") ||
       beetle_game_database_lookup("SCUS-9418A") ||
       beetle_game_database_lookup("SCUS-94182") ||
       beetle_game_database_lookup("SLUS-01323"))
   {
      fprintf(stderr, "FAIL retail Armored Core has a controller override\n");
      failures++;
   }

   if (beetle_game_database_lookup("PAPX-90054") ||
       beetle_game_database_lookup("SCUS-94588"))
   {
      fprintf(stderr, "FAIL metadata-only disc entered runtime database\n");
      failures++;
   }

   entry = require_entry("SCUS-94488");
   if (entry && !strstr(beetle_game_database_title(entry), "Gran Turismo 2"))
   {
      fprintf(stderr, "FAIL SCUS-94488 title=%s\n",
            beetle_game_database_title(entry));
      failures++;
   }

   if (beetle_game_database_lookup("SLUS-00000"))
   {
      fprintf(stderr, "FAIL unknown serial unexpectedly found\n");
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
