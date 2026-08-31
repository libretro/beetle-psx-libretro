#ifndef __INPUT_H__
#define __INPUT_H__

#include <boolean.h>
#include <libretro.h>
#include "mednafen/psx/frontio.h"

#define RETRO_DEVICE_PS_CONTROLLER         RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 0)
#define RETRO_DEVICE_PS_DUALSHOCK          RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 1)
#define RETRO_DEVICE_PS_ANALOG             RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 0)
#define RETRO_DEVICE_PS_ANALOG_JOYSTICK    RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 2)
#define RETRO_DEVICE_PS_GUNCON             RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_LIGHTGUN, 0)
#define RETRO_DEVICE_PS_JUSTIFIER          RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_LIGHTGUN, 1)
#define RETRO_DEVICE_PS_MOUSE              RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_MOUSE, 0)
#define RETRO_DEVICE_PS_NEGCON             RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 3)
#define RETRO_DEVICE_PS_NEGCON_RUMBLE      RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 4)

#ifdef __cplusplus
extern "C" {
#endif

/* These input routines tell libretro about PlayStation peripherals */
/* and map input from the abstract 'retropad' into PlayStation land. */

extern void input_init_env( retro_environment_t environ_cb );

extern void input_init(void);

extern void input_set_fio( FrontIO* fio );

extern void input_init_calibration(void);
extern void input_enable_calibration( bool enable );

extern void input_set_env( retro_environment_t environ_cb );

extern void input_set_mouse_sensitivity( int percent );
extern void input_set_gun_cursor( int cursor );

extern void input_set_negcon_deadzone( int deadzone );
extern void input_set_negcon_linearity( int linearity );

extern void input_set_player_count( unsigned players );

extern bool input_set_controller_port_compatibility(
      unsigned port, uint32_t supported_controllers);

extern unsigned input_get_player_count(void);

void input_update(bool supports_bitmasks, retro_input_state_t input_state_cb );

enum
{
   SETTING_GUN_INPUT_LIGHTGUN,
   SETTING_GUN_INPUT_POINTER,
};
extern int gun_input_mode;

#ifdef __cplusplus
}
#endif

#endif
