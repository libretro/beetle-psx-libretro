#include "libretro_game_database.h"

#include <string.h>

#define PSX_GAME(redump_id, serial, source_serial, category, version, title)
#define PSX_GAME_COMPAT(redump_id, serial, source_serial, category, version, \
      title, settings) \
   { serial, title, settings },

static const struct beetle_game_database_entry game_database_entries[] = {
#include "database/psx_games.inc"
};

#undef PSX_GAME_COMPAT
#undef PSX_GAME

static int is_ascii_letter(char value)
{
   return (value >= 'A' && value <= 'Z') ||
      (value >= 'a' && value <= 'z');
}

static int is_ascii_digit(char value)
{
   return value >= '0' && value <= '9';
}

static char ascii_toupper(char value)
{
   return value >= 'a' && value <= 'z' ? value - ('a' - 'A') : value;
}

int beetle_game_database_normalize_serial(const char *source,
      char serial[BEETLE_DISC_SERIAL_SIZE])
{
   unsigned i;
   unsigned suffix_offset;
   size_t source_length;

   if (!source || !serial)
      return 0;

   source_length = strlen(source);
   if (source_length < 10)
      return 0;

   for (i = 0; i < 4; i++)
      if (!is_ascii_letter(source[i]))
         return 0;

   if (source[4] != '_' && source[4] != '-')
      return 0;
   if (!is_ascii_digit(source[5]) || !is_ascii_digit(source[6]) ||
       !is_ascii_digit(source[7]))
      return 0;

   suffix_offset = source[8] == '.' ? 9 : 8;
   if (source_length < suffix_offset + 2)
      return 0;
   if (!is_ascii_digit(source[suffix_offset]) ||
       (!is_ascii_digit(source[suffix_offset + 1]) &&
        !is_ascii_letter(source[suffix_offset + 1])))
      return 0;

   for (i = 0; i < 4; i++)
      serial[i] = ascii_toupper(source[i]);
   serial[4] = '-';
   serial[5] = source[5];
   serial[6] = source[6];
   serial[7] = source[7];
   serial[8] = source[suffix_offset];
   serial[9] = ascii_toupper(source[suffix_offset + 1]);
   serial[10] = '\0';
   return 1;
}

const struct beetle_game_database_entry *beetle_game_database_lookup(
      const char *serial)
{
   size_t i;

   if (!serial || !serial[0])
      return NULL;

   for (i = 0; i < sizeof(game_database_entries) /
         sizeof(game_database_entries[0]); i++)
      if (!strcmp(serial, game_database_entries[i].serial))
         return &game_database_entries[i];

   return NULL;
}

const char *beetle_game_database_title(
      const struct beetle_game_database_entry *entry)
{
   if (!entry)
      return "Unknown";
   return entry->title;
}

size_t beetle_game_database_count(void)
{
   return sizeof(game_database_entries) / sizeof(game_database_entries[0]);
}
