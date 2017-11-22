#ifndef __INPUT_H__
#define __INPUT_H__

#include "libretro.h"
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
extern void input_set_gun_trigger( bool use_rmb );

extern void input_set_player_count( unsigned players );

extern unsigned input_get_player_count();

extern void input_update( retro_input_state_t input_state_cb );

#endif
