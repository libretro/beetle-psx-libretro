#include <stddef.h>

#include "libretro.h"
#include "input.h"
#include "input_compatibility.h"
#include "libretro_game_database.h"

struct controller_compatibility_map
{
   unsigned device;
   uint32_t flag;
};

/* This order defines the fallback priority for controller profiles. */
static const struct controller_compatibility_map controller_map[] =
{
   { RETRO_DEVICE_JOYPAD,                 BEETLE_DB_CTRL_DIGITAL },
   { RETRO_DEVICE_PS_DUALSHOCK,           BEETLE_DB_CTRL_DUALSHOCK },
   { RETRO_DEVICE_PS_ANALOG,              BEETLE_DB_CTRL_ANALOG },
   { RETRO_DEVICE_PS_ANALOG_JOYSTICK,     BEETLE_DB_CTRL_ANALOG_JOYSTICK },
   { RETRO_DEVICE_PS_GUNCON,              BEETLE_DB_CTRL_GUNCON },
   { RETRO_DEVICE_PS_JUSTIFIER,           BEETLE_DB_CTRL_JUSTIFIER },
   { RETRO_DEVICE_PS_MOUSE,               BEETLE_DB_CTRL_MOUSE },
   { RETRO_DEVICE_PS_NEGCON,              BEETLE_DB_CTRL_NEGCON },
   { RETRO_DEVICE_PS_NEGCON_RUMBLE,       BEETLE_DB_CTRL_NEGCON_RUMBLE },
};

static uint32_t game_db_controller_flag_for_device(unsigned device)
{
   size_t i;

   if (device == RETRO_DEVICE_PS_CONTROLLER)
      device = RETRO_DEVICE_JOYPAD;

   for (i = 0; i < sizeof(controller_map) / sizeof(controller_map[0]); i++)
   {
      if (controller_map[i].device == device)
         return controller_map[i].flag;
   }

   return BEETLE_DB_CTRL_NONE;
}

unsigned input_resolve_compatible_controller(
      unsigned requested_device, uint32_t supported_controllers)
{
   uint32_t requested_flag;
   size_t i;

   supported_controllers &= BEETLE_DB_CTRL_ALL;
   if (!supported_controllers || requested_device == RETRO_DEVICE_NONE)
      return requested_device;

   requested_flag = game_db_controller_flag_for_device(requested_device);
   if (requested_flag & supported_controllers)
      return requested_device;

   for (i = 0; i < sizeof(controller_map) / sizeof(controller_map[0]); i++)
   {
      if (controller_map[i].flag & supported_controllers)
         return controller_map[i].device;
   }

   return requested_device;
}
