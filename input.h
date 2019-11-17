#ifndef __INPUT_H__
#define __INPUT_H__

#include <boolean.h>
#include <libretro.h>
#include "mednafen/psx/frontio.h"

// These input routines tell libretro about PlayStation peripherals
// and map input from the abstract 'retropad' into PlayStation land.

extern void input_init_env( retro_environment_t environ_cb );

extern void input_init();

extern void input_set_fio( FrontIO* fio );

extern void input_init_calibration();
extern void input_enable_calibration( bool enable );

extern void input_set_env( retro_environment_t environ_cb );

extern void input_set_mouse_sensitivity( int percent );
extern void input_set_gun_cursor( int cursor );

extern void input_set_negcon_deadzone( int deadzone );
extern void input_set_negcon_linearity( int linearity );

extern void input_set_player_count( unsigned players );

extern unsigned input_get_player_count();

void input_update(bool supports_bitmasks, retro_input_state_t input_state_cb );

enum
{
   SETTING_GUN_INPUT_LIGHTGUN,
   SETTING_GUN_INPUT_POINTER,
};
extern int gun_input_mode;

#endif
