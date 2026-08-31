#ifndef LIBRETRO_GAME_DATABASE_H__
#define LIBRETRO_GAME_DATABASE_H__

#include <stddef.h>
#include <stdint.h>

#define BEETLE_DISC_SERIAL_SIZE 11

enum psx_compatibility_setting
{
   PSX_COMPAT_PGXP_MEM_CPU            = 1u << 0,
   PSX_COMPAT_PGXP_PCT_ON             = 1u << 1,
   PSX_COMPAT_PGXP_PCT_OFF            = 1u << 2,
   PSX_COMPAT_PGXP_CULLING_ON         = 1u << 3,
   PSX_COMPAT_FBWRITE_FIFO_DELAY      = 1u << 4,
   PSX_COMPAT_PGXP_CACHE_ON           = 1u << 5,
   PSX_COMPAT_PGXP_CACHE_OFF          = 1u << 6
};

#define PSX_COMPAT_CD_SPEED_SHIFT 8
#define PSX_COMPAT_CD_SPEED_MASK (0x1fu << \
      PSX_COMPAT_CD_SPEED_SHIFT)
#define PSX_COMPAT_MAX_CD_SPEED(value) \
   (((uint32_t)(value) & 0x1fu) << PSX_COMPAT_CD_SPEED_SHIFT)
#define PSX_COMPAT_GET_MAX_CD_SPEED(settings) \
   (((settings) & PSX_COMPAT_CD_SPEED_MASK) >> \
      PSX_COMPAT_CD_SPEED_SHIFT)

struct beetle_game_database_entry
{
   const char *serial;
   const char *title;
   uint32_t settings;
};

int beetle_game_database_normalize_serial(const char *source,
      char serial[BEETLE_DISC_SERIAL_SIZE]);
const struct beetle_game_database_entry *beetle_game_database_lookup(
      const char *serial);
const char *beetle_game_database_title(
      const struct beetle_game_database_entry *entry);
size_t beetle_game_database_count(void);

#endif
