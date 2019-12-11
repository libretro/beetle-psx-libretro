#include "libretro.h"
#include "mednafen/mednafen-types.h"
#include <math.h>
#include <stdio.h>
#include <algorithm>
#include "mednafen/git.h"
#include "mednafen/psx/frontio.h"
#include "input.h"
#include "beetle_psx_globals.h"

//------------------------------------------------------------------------------
// Locals
//------------------------------------------------------------------------------

static retro_environment_t environ_cb; // cached during input_init_env
static retro_rumble_interface rumble; // acquired during input_init_env

static FrontIO* FIO; // cached in input_set_fio

#define MAX_CONTROLLERS 8

static unsigned players = 2;
static bool enable_analog_calibration = false;
static float mouse_sensitivity = 1.0f;
static int gun_cursor = FrontIO::SETTING_GUN_CROSSHAIR_CROSS;

int gun_input_mode = SETTING_GUN_INPUT_LIGHTGUN;

// Touchscreen Lightgun Sensitivity
static int pointer_pressed = 0;
static const int POINTER_PRESSED_CYCLES = 4;
static int pointer_cycles_after_released = 0;
static int pointer_pressed_last_x = 0;
static int pointer_pressed_last_y = 0;

// NegCon adjustment parameters
// > The NegCon 'twist' action is somewhat awkward when mapped
//   to a standard analog stick -> user should be able to tweak
//   response/deadzone for comfort
// > When response is linear, 'additional' deadzone (set here)
//   may be left at zero, since this is normally handled via in-game
//   options menus
// > When response is non-linear, deadzone should be set to match the
//   controller being used (otherwise precision may be lost)
// > negcon_linearity:
//   - 1: Response is linear - recommended when using racing wheel
//        peripherals, not recommended for standard gamepads
//   - 2: Response is quadratic - optimal setting for gamepads
//   - 3: Response is cubic - enables precise fine control, but
//        difficult to use...
#define NEGCON_RANGE 0x7FFF
static int negcon_deadzone = 0;
static int negcon_linearity = 1;

typedef union
{
   uint8_t u8[ 10 * sizeof( uint32_t ) ];
   uint32_t u32[ 10 ]; /* 1 + 8 + 1 */
   uint16_t gun_pos[ 2 ];
   uint16_t buttons;
}
INPUT_DATA;

// Controller state buffer (per player)
static INPUT_DATA input_data[ MAX_CONTROLLERS ] = {0};

struct analog_calibration
{
   float left;
   float right;
   float twist;
};

static struct analog_calibration analog_calibration[MAX_CONTROLLERS];

// Controller type (per player)
static uint32_t input_type[ MAX_CONTROLLERS ] = {0};


//------------------------------------------------------------------------------
// Supported Devices
//------------------------------------------------------------------------------

#define RETRO_DEVICE_PS_CONTROLLER         RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 0)
#define RETRO_DEVICE_PS_DUALSHOCK          RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 1)
#define RETRO_DEVICE_PS_ANALOG             RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 0)
#define RETRO_DEVICE_PS_ANALOG_JOYSTICK    RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 2)
#define RETRO_DEVICE_PS_GUNCON             RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_LIGHTGUN, 0)
#define RETRO_DEVICE_PS_JUSTIFIER          RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_LIGHTGUN, 1)
#define RETRO_DEVICE_PS_MOUSE              RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_MOUSE, 0)
#define RETRO_DEVICE_PS_NEGCON             RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 3)

enum { INPUT_DEVICE_TYPES_COUNT = 1 /*none*/ + 8 }; // <-- update me!

static const struct retro_controller_description input_device_types[ INPUT_DEVICE_TYPES_COUNT ] =
{
   { "PlayStation Controller", RETRO_DEVICE_JOYPAD },
   { "DualShock", RETRO_DEVICE_PS_DUALSHOCK },
   { "Analog Controller", RETRO_DEVICE_PS_ANALOG },
   { "Analog Joystick", RETRO_DEVICE_PS_ANALOG_JOYSTICK },
   { "Guncon / G-Con 45", RETRO_DEVICE_PS_GUNCON },
   { "Justifier", RETRO_DEVICE_PS_JUSTIFIER },
   { "Mouse", RETRO_DEVICE_PS_MOUSE },
   { "neGcon", RETRO_DEVICE_PS_NEGCON },
   { NULL, 0 },
};

static const struct retro_controller_info ports8[ 8 + 1 ] =
{
   { input_device_types, INPUT_DEVICE_TYPES_COUNT },
   { input_device_types, INPUT_DEVICE_TYPES_COUNT },
   { input_device_types, INPUT_DEVICE_TYPES_COUNT },
   { input_device_types, INPUT_DEVICE_TYPES_COUNT },
   { input_device_types, INPUT_DEVICE_TYPES_COUNT },
   { input_device_types, INPUT_DEVICE_TYPES_COUNT },
   { input_device_types, INPUT_DEVICE_TYPES_COUNT },
   { input_device_types, INPUT_DEVICE_TYPES_COUNT },
   { 0 },
};

static const struct retro_controller_info ports5[ 5 + 1 ] =
{
   { input_device_types, INPUT_DEVICE_TYPES_COUNT },
   { input_device_types, INPUT_DEVICE_TYPES_COUNT },
   { input_device_types, INPUT_DEVICE_TYPES_COUNT },
   { input_device_types, INPUT_DEVICE_TYPES_COUNT },
   { input_device_types, INPUT_DEVICE_TYPES_COUNT },
   { 0 },
};

static const struct retro_controller_info ports2[ 2 + 1 ] =
{
   { input_device_types, INPUT_DEVICE_TYPES_COUNT },
   { input_device_types, INPUT_DEVICE_TYPES_COUNT },
   { 0 },
};


//------------------------------------------------------------------------------
// Mapping Helpers
//------------------------------------------------------------------------------

/* Controller (default) */
enum { INPUT_MAP_CONTROLLER_SIZE = 16 };
static const unsigned input_map_controller[ INPUT_MAP_CONTROLLER_SIZE ] =
{
 // libretro input                  at position   || maps to PS1         on bit
 //-----------------------------------------------------------------------------
#ifdef MSB_FIRST
   RETRO_DEVICE_ID_JOYPAD_L2,       // L-trigger    -> L2
   RETRO_DEVICE_ID_JOYPAD_R2,       // R-trigger    -> R2
   RETRO_DEVICE_ID_JOYPAD_L,        // L-shoulder   -> L1
   RETRO_DEVICE_ID_JOYPAD_R,        // R-shoulder   -> R1
   RETRO_DEVICE_ID_JOYPAD_X,        // X(top)       -> Triangle
   RETRO_DEVICE_ID_JOYPAD_A,        // A(right)     -> Circle
   RETRO_DEVICE_ID_JOYPAD_B,        // B(down)      -> Cross
   RETRO_DEVICE_ID_JOYPAD_Y,        // Y(left)      -> Square
   RETRO_DEVICE_ID_JOYPAD_SELECT,   // Select       -> Select
   RETRO_DEVICE_ID_JOYPAD_L3,       // L-thumb      -> L3
   RETRO_DEVICE_ID_JOYPAD_R3,       // R-thumb      -> R3
   RETRO_DEVICE_ID_JOYPAD_START,    // Start        -> Start
   RETRO_DEVICE_ID_JOYPAD_UP,       // Pad-Up       -> Pad-Up
   RETRO_DEVICE_ID_JOYPAD_RIGHT,    // Pad-Right    -> Pad-Right
   RETRO_DEVICE_ID_JOYPAD_DOWN,     // Pad-Down     -> Pad-Down
   RETRO_DEVICE_ID_JOYPAD_LEFT,     // Pad-Left     -> Pad-Left
#else
   RETRO_DEVICE_ID_JOYPAD_SELECT,   // Select       -> Select               0
   RETRO_DEVICE_ID_JOYPAD_L3,       // L-thumb      -> L3                   1
   RETRO_DEVICE_ID_JOYPAD_R3,       // R-thumb      -> R3                   2
   RETRO_DEVICE_ID_JOYPAD_START,    // Start        -> Start                3
   RETRO_DEVICE_ID_JOYPAD_UP,       // Pad-Up       -> Pad-Up               4
   RETRO_DEVICE_ID_JOYPAD_RIGHT,    // Pad-Right    -> Pad-Right            5
   RETRO_DEVICE_ID_JOYPAD_DOWN,     // Pad-Down     -> Pad-Down             6
   RETRO_DEVICE_ID_JOYPAD_LEFT,     // Pad-Left     -> Pad-Left             7
   RETRO_DEVICE_ID_JOYPAD_L2,       // L-trigger    -> L2                   8
   RETRO_DEVICE_ID_JOYPAD_R2,       // R-trigger    -> R2                   9
   RETRO_DEVICE_ID_JOYPAD_L,        // L-shoulder   -> L1                  10
   RETRO_DEVICE_ID_JOYPAD_R,        // R-shoulder   -> R1                  11
   RETRO_DEVICE_ID_JOYPAD_X,        // X(top)       -> Triangle            12
   RETRO_DEVICE_ID_JOYPAD_A,        // A(right)     -> Circle              13
   RETRO_DEVICE_ID_JOYPAD_B,        // B(down)      -> Cross               14
   RETRO_DEVICE_ID_JOYPAD_Y,        // Y(left)      -> Square              15
#endif
};


//------------------------------------------------------------------------------
// Local Functions
//------------------------------------------------------------------------------

// Return the normalized distance between the origin and the current
// (x,y) analog stick position
static float analog_radius(int x, int y)
{
   float fx = ((float)x) / 0x8000;
   float fy = ((float)y) / 0x8000;

   return sqrtf(fx * fx + fy * fy);
}

static float analog_deflection(int x)
{
   float fx = ((float)x) / 0x8000;

   return fabsf(fx);
}

static void analog_scale(uint32_t *v, float s)
{
   *v *= s;

   if (*v > 0x7fff)
      *v = 0x7fff;
}

static void SetInput(int port, const char *type, void *ptr)
{
   FIO->SetInput(port, type, ptr);
}

static uint16_t get_analog_button( retro_input_state_t input_state_cb,
                                   int player_index,
                                   int id )
{
   uint16_t button;

   // NOTE: Analog buttons were added Nov 2017. Not all front-ends support this
   // feature (or pre-date it) so we need to handle this in a graceful way.

   // First, try and get an analog value using the new libretro API constant
   button = input_state_cb( player_index,
                            RETRO_DEVICE_ANALOG,
                            RETRO_DEVICE_INDEX_ANALOG_BUTTON,
                            id );

   if ( button == 0 )
   {
      // If we got exactly zero, we're either not pressing the button, or the front-end
      // is not reporting analog values. We need to do a second check using the classic
      // digital API method, to at least get some response - better than nothing.

      // NOTE: If we're really just not holding the button, we're still going to get zero.

      button = input_state_cb( player_index,
                               RETRO_DEVICE_JOYPAD,
                               0,
                               id ) ? 0x7FFF : 0;
   }

   return button;
}


//------------------------------------------------------------------------------
// Global Functions
//------------------------------------------------------------------------------

void input_init_env( retro_environment_t _environ_cb )
{
   // Cache this
   environ_cb = _environ_cb;

   struct retro_input_descriptor desc[] =
   {
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Cross" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Circle" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Triangle" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Square" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L1" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L2" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,    "L3" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R1" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R2" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,    "R3" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,   "Select" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },
      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Left Analog X" },
      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Left Analog Y" },
      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },
      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" },


      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Cross" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Circle" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Triangle" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Square" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L1" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L2" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,    "L3" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R1" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R2" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,    "R3" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,   "Select" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },
      { 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Left Analog X" },
      { 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Left Analog Y" },
      { 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },
      { 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" },

      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Cross" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Circle" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Triangle" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Square" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L1" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L2" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,    "L3" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R1" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R2" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,    "R3" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,   "Select" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },
      { 2, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Left Analog X" },
      { 2, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Left Analog Y" },
      { 2, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },
      { 2, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" },

      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Cross" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Circle" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Triangle" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Square" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L1" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L2" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,    "L3" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R1" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R2" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,    "R3" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,   "Select" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },
      { 3, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Left Analog X" },
      { 3, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Left Analog Y" },
      { 3, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },
      { 3, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" },

      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Cross" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Circle" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Triangle" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Square" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L1" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L2" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,    "L3" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R1" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R2" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,    "R3" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,   "Select" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },
      { 4, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Left Analog X" },
      { 4, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Left Analog Y" },
      { 4, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },
      { 4, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" },

      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Cross" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Circle" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Triangle" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Square" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L1" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L2" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,    "L3" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R1" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R2" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,    "R3" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,   "Select" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },
      { 5, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Left Analog X" },
      { 5, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Left Analog Y" },
      { 5, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },
      { 5, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" },

      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Cross" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Circle" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Triangle" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Square" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L1" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L2" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,    "L3" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R1" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R2" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,    "R3" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,   "Select" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },
      { 6, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Left Analog X" },
      { 6, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Left Analog Y" },
      { 6, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },
      { 6, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" },

      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Cross" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Circle" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Triangle" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Square" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L1" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L2" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,    "L3" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R1" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R2" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,    "R3" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,   "Select" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },
      { 7, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Left Analog X" },
      { 7, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Left Analog Y" },
      { 7, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },
      { 7, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" },

      { 0 },
   };

   environ_cb( RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc );

   if ( environ_cb( RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE, &rumble ) && log_cb )
      log_cb(RETRO_LOG_INFO, "Rumble interface supported!\n");
}

void input_set_env( retro_environment_t _environ_cb )
{
   switch ( players )
   {
      case 8:
         _environ_cb( RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports8 );
         break;

      case 5:
         _environ_cb( RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports5 );
         break;

      default:
      case 2:
         _environ_cb( RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports2 );
         break;

   } /* switch ( players ) */
}

void input_init()
{
   // Initialise to default and bind input buffers to PS1 emulation.
   for ( unsigned i = 0; i < MAX_CONTROLLERS; ++i )
   {
      input_type[ i ] = RETRO_DEVICE_JOYPAD;

      SetInput( i, "gamepad", (uint8*)&input_data[ i ] );
   }
}

void input_set_fio( FrontIO* fio )
{
   FIO = fio;
}

void input_init_calibration()
{
   for ( unsigned i = 0; i < MAX_CONTROLLERS; i++ )
   {
      analog_calibration[ i ].left = 0.7f;
      analog_calibration[ i ].right = 0.7f;
      analog_calibration[ i ].twist = 0.7f;
   }
}

void input_enable_calibration( bool enable )
{
   enable_analog_calibration = enable;
}

void input_set_player_count( unsigned _players )
{
   players = _players;
   input_set_env( environ_cb );
}

void input_set_mouse_sensitivity( int percent )
{
   if ( percent > 0 && percent <= 200 )
      mouse_sensitivity = (float)percent / 100.0f;
}

void input_set_gun_cursor( int cursor )
{
   gun_cursor = cursor;
   if ( FIO )
   {
      // todo -- support multiple guns.
      for ( int port = 0; port < 8; ++port )
         FIO->SetCrosshairsCursor( port, gun_cursor );
   }
}

void input_set_negcon_deadzone( int deadzone )
{
   negcon_deadzone = deadzone;
}

void input_set_negcon_linearity( int linearity )
{
   negcon_linearity = linearity;
}

unsigned input_get_player_count()
{
   return players;
}

void input_handle_lightgun_touchscreen( INPUT_DATA *p_input, int iplayer, retro_input_state_t input_state_cb )
{
   int gun_x_raw = input_state_cb( iplayer, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X);
   int gun_y_raw = input_state_cb( iplayer, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y);

   // Comments by hiddenasbestos
   // .. scale into screen space:
   // NOTE: the scaling here is semi-guesswork, need to re-write.
   // TODO: Test with PAL games.
   // Can also implement scaling with initial/last scanline

   const int scale_x = (crop_overscan ? 2560 : 2800);
   const int scale_y = (content_is_pal ? 288 : 240);

   int gun_x = (( ( gun_x_raw + 0x7fff ) * scale_x ) / (0x7fff << 1)) + (crop_overscan ? 120 : 0);
   int gun_y = ( ( gun_y_raw + 0x7fff ) * scale_y ) / (0x7fff << 1) + (content_is_pal ? 4 : 0);

#if 0
   int is_offscreen = 0;
#endif

   // Handle offscreen by checking corrected x and y values
   if ( gun_x == 0 || gun_y == 0 )
   {
#if 0
      is_offscreen = 1;
#endif

      gun_x = -16384; // magic position to disable cross-hair drawing.
      gun_y = -16384;
   }

   // Touch sensitivity: Keep the gun position held for a fixed number of cycles after touch is released
   // because a very light touch can result in a misfire
   if ( pointer_cycles_after_released > 0 && pointer_cycles_after_released < POINTER_PRESSED_CYCLES )
   {
      pointer_cycles_after_released++;
      p_input->gun_pos[ 0 ] = pointer_pressed_last_x;
      p_input->gun_pos[ 1 ] = pointer_pressed_last_y;
      return;
   }

   // trigger
   if ( input_state_cb( iplayer, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED ) )
   {
      pointer_pressed = 1;
      pointer_cycles_after_released = 0;
      pointer_pressed_last_x = gun_x;
      pointer_pressed_last_y = gun_y;
   } else if ( pointer_pressed ) {
      pointer_cycles_after_released++;
      pointer_pressed = 0;
      p_input->gun_pos[ 0 ] = pointer_pressed_last_x;
      p_input->gun_pos[ 1 ] = pointer_pressed_last_y;
      p_input->u8[4] &= ~0x1;
      return;
   }

   // position
   p_input->gun_pos[ 0 ] = gun_x;
   p_input->gun_pos[ 1 ] = gun_y;

   // buttons
   p_input->u8[ 4 ] = 0;

   // use multi-touch to support different button inputs:
   // 3-finger touch: START button
   // 2-finger touch: Reload
   // offscreen touch: Reload
   int touch_count = input_state_cb( iplayer, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_COUNT );
   if ( touch_count == 1 )
      p_input->u8[ 4 ] |= 0x1;

   if ( input_type[ iplayer ] == RETRO_DEVICE_PS_JUSTIFIER )
   {
      // Justifier 'Aux'
      if ( touch_count == 2 )
         p_input->u8[ 4 ] |= 0x2;

      // Justifier 'Start'
      if ( touch_count == 3 )
         p_input->u8[ 4 ] |= 0x4;
   }
   else
   {
      // Guncon 'A'
      if ( touch_count == 2 )
         p_input->u8[ 4 ] |= 0x2;

      // Guncon 'B'
      if ( touch_count == 3 )
         p_input->u8[ 4 ] |= 0x4;

      // Guncon 'A' + 'B'
      if ( touch_count == 4 )
      {
         p_input->u8[ 4 ] |= 0x2;
         p_input->u8[ 4 ] |= 0x4;
      }
   }
}

void input_handle_lightgun( INPUT_DATA *p_input, int iplayer, retro_input_state_t input_state_cb )
{
   uint8_t shot_type;
   int gun_x, gun_y;
   int forced_reload = input_state_cb( iplayer, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_RELOAD );

   // off-screen?
   if ( input_state_cb( iplayer, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN ) || forced_reload )
   {
      shot_type = 0x8; // off-screen shot

      gun_x = -16384; // magic position to disable cross-hair drawing.
      gun_y = -16384;
   }
   else
   {
      shot_type = 0x1; // on-screen shot

      int gun_x_raw = input_state_cb( iplayer, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X );
      int gun_y_raw = input_state_cb( iplayer, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y );

      // Comments by hiddenasbestos
      // .. scale into screen space:
      // NOTE: the scaling here is semi-guesswork, need to re-write.
      // TODO: Test with PAL games.
      // Can also implement scaling with initial/last scanline

      const int scale_x = (crop_overscan ? 2560 : 2800);
      const int scale_y = (content_is_pal ? 288 : 240);

      gun_x = (( ( gun_x_raw + 0x7fff ) * scale_x ) / (0x7fff << 1)) + (crop_overscan ? 120 : 0);
      gun_y = ( ( gun_y_raw + 0x7fff ) * scale_y ) / (0x7fff << 1) + (content_is_pal ? 4 : 0);
   }

   // position
   p_input->gun_pos[ 0 ] = gun_x;
   p_input->gun_pos[ 1 ] = gun_y;

   // buttons
   p_input->u8[ 4 ] = 0;

   // trigger
   if ( input_state_cb( iplayer, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_TRIGGER ) || forced_reload )
      p_input->u8[ 4 ] |= shot_type;

   if ( input_type[ iplayer ] == RETRO_DEVICE_PS_JUSTIFIER )
   {
      // Justifier 'Aux'
      if ( input_state_cb( iplayer, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_AUX_A ) )
         p_input->u8[ 4 ] |= 0x2;

      // Justifier 'Start'
      if ( input_state_cb( iplayer, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_START ) )
         p_input->u8[ 4 ] |= 0x4;
   }
   else
   {
      // Guncon 'A'
      if ( input_state_cb( iplayer, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_AUX_A ) )
         p_input->u8[ 4 ] |= 0x2;

      // Guncon 'B'
      if ( input_state_cb( iplayer, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_AUX_B ) )
         p_input->u8[ 4 ] |= 0x4;
   }
}

void input_update(bool libretro_supports_bitmasks, retro_input_state_t input_state_cb )
{
   // For each player (logical controller)
   for ( unsigned iplayer = 0; iplayer < players; ++iplayer )
   {
      INPUT_DATA* p_input = &(input_data[ iplayer ]);

      switch ( input_type[ iplayer ] )
      {

         default:
            p_input->buttons = 0;
            break;

         case RETRO_DEVICE_JOYPAD:
         case RETRO_DEVICE_PS_CONTROLLER:
         case RETRO_DEVICE_PS_DUALSHOCK:
         case RETRO_DEVICE_PS_ANALOG:
         case RETRO_DEVICE_PS_ANALOG_JOYSTICK:

            // Use fixed lookup table to map RetroPad inputs to PlayStation input bitmap.
            p_input->buttons = 0;
            if (libretro_supports_bitmasks)
            {
               int16_t ret = input_state_cb(iplayer, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
               for ( unsigned i = 0; i < INPUT_MAP_CONTROLLER_SIZE; i++ )
                  p_input->buttons |= (ret & (1 << input_map_controller[ i ] ))
                     ? ( 1 << i ) : 0;
            }
            else
            {
               for ( unsigned i = 0; i < INPUT_MAP_CONTROLLER_SIZE; i++ )
               {
                  p_input->buttons |=
                     input_state_cb( iplayer, RETRO_DEVICE_JOYPAD, 0, input_map_controller[ i ] )
                     ? ( 1 << i ) : 0;
               }
            }

            break;

         case RETRO_DEVICE_PS_GUNCON:
         case RETRO_DEVICE_PS_JUSTIFIER:
            if ( gun_input_mode == SETTING_GUN_INPUT_POINTER )
               input_handle_lightgun_touchscreen( p_input, iplayer, input_state_cb );
            else
               // RETRO_DEVICE_LIGHTGUN is default
               input_handle_lightgun( p_input, iplayer, input_state_cb );

            // allow guncon buttons to be pressed by gamepad while using lightgun
            // Joypad L/R/A buttons are mapped to Guncon 'A'
            // The idea is to be able to use the controller in tandem with some games
            // like Time Crisis where you need to hold a button down and fire
            if ( input_state_cb( iplayer, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A ) ||
                  input_state_cb( iplayer, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L ) ||
                  input_state_cb( iplayer, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R ) )
               // Guncon 'A'
               p_input->u8[ 4 ] |= 0x2;

            break;

         case RETRO_DEVICE_PS_MOUSE:

            // Simple two-button mouse.
            p_input->u32[ 2 ] = 0;
            if ( input_state_cb( iplayer, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT ) )
               p_input->u32[ 2 ] |= ( 1 << 1 ); // Left
            if ( input_state_cb( iplayer, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT ) )
               p_input->u32[ 2 ] |= ( 1 << 0 ); // Right

            // Relative input.
            {
               // mouse input
               int dx_raw = input_state_cb( iplayer, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X );
               int dy_raw = input_state_cb( iplayer, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y );

               p_input->u32[ 0 ] = (int)roundf( dx_raw * mouse_sensitivity );
               p_input->u32[ 1 ] = (int)roundf( dy_raw * mouse_sensitivity );
            }

            break;

         case RETRO_DEVICE_PS_NEGCON:

            // Analog Inputs
            {
               uint16_t button_ii = std::max(
                     get_analog_button( input_state_cb, iplayer, RETRO_DEVICE_ID_JOYPAD_L2 ),
                     get_analog_button( input_state_cb, iplayer, RETRO_DEVICE_ID_JOYPAD_Y )
                     );

               uint16_t button_i = std::max(
                     get_analog_button( input_state_cb, iplayer, RETRO_DEVICE_ID_JOYPAD_R2 ),
                     get_analog_button( input_state_cb, iplayer, RETRO_DEVICE_ID_JOYPAD_B )
                     );
               uint16_t left_shoulder = get_analog_button( input_state_cb, iplayer, RETRO_DEVICE_ID_JOYPAD_L );

               p_input->u32[ 3 ] = button_i; // Analog button I
               p_input->u32[ 4 ] = button_ii; // Analog button II
               p_input->u32[ 5 ] = left_shoulder; // Analog shoulder (left only!)
            }

            // Twist
            {
               int analog_left_x = input_state_cb( iplayer, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,
                     RETRO_DEVICE_ID_ANALOG_X);

               // Account for deadzone
               if (analog_left_x > negcon_deadzone)
                  analog_left_x = analog_left_x - negcon_deadzone;
               else if (analog_left_x < -negcon_deadzone)
                  analog_left_x = analog_left_x + negcon_deadzone;
               else
                  analog_left_x = 0;

               // Convert to an 'amplitude' [-1.0,1.0]
               float analog_left_x_amplitude = (float)analog_left_x / (float)(NEGCON_RANGE - negcon_deadzone);

               // Handle 'analog self-calibration'...
               // NB: This seems pointless, since all it does is arbitrarily
               // reduce the precision of 'twist' input (making games rather
               // unplayable). Someone, however, must have thought it was a
               // good idea at some point, so we'll leave the basic functionality
               // in place...
               struct analog_calibration *calibration = &analog_calibration[ iplayer ];
               if ( enable_analog_calibration )
               {
                  // Compute the current stick deflection
                  float twist = fabsf(analog_left_x_amplitude);

                  // We recalibrate when we find a new max value for the sticks
                  if ( twist > analog_calibration->twist )
                  {
                     analog_calibration->twist = twist;
                     log_cb(RETRO_LOG_DEBUG, "Recalibrating twist, deflection: %f\n", twist);
                  }

                  // NOTE: This value was copied from the DualShock code below. Needs confirmation.
                  static const float neGcon_analog_deflection = 1.35f;

                  // Now compute the scaling factor to apply to convert the
                  // emulator's controller coordinates to a native neGcon range.
                  float twist_scaling = neGcon_analog_deflection / analog_calibration->twist;

                  analog_left_x_amplitude = analog_left_x_amplitude * twist_scaling;
               }
               else
               {
                  // Reset the calibration. Since we only increase the
                  // calibration coordinates we can start with a reasonably
                  // small value.
                  analog_calibration->twist = 0.7;
               }

               // Safety check
               // (also fixes range when above 'analog self-calibration' twist_scaling
               // is applied)
               analog_left_x_amplitude = analog_left_x_amplitude < -1.0f ? -1.0f : analog_left_x_amplitude;
               analog_left_x_amplitude = analog_left_x_amplitude > 1.0f ? 1.0f : analog_left_x_amplitude;

               // Adjust response
               if (negcon_linearity == 2)
               {
                  if (analog_left_x_amplitude < 0.0)
                     analog_left_x_amplitude = -(analog_left_x_amplitude * analog_left_x_amplitude);
                  else
                     analog_left_x_amplitude = analog_left_x_amplitude * analog_left_x_amplitude;
               }
               else if (negcon_linearity == 3)
                  analog_left_x_amplitude = analog_left_x_amplitude * analog_left_x_amplitude * analog_left_x_amplitude;

               // Convert back from an 'amplitude' [-1.0,1.0] to a 'range' [-0x7FFF,0x7FFF]
               analog_left_x = (int)(analog_left_x_amplitude * NEGCON_RANGE);

               uint32_t twist_left  = analog_left_x < 0 ? -analog_left_x : 0;
               uint32_t twist_right = analog_left_x > 0 ?  analog_left_x : 0;

               p_input->u32[ 1 ] = twist_right; // Twist Right
               p_input->u32[ 2 ] = twist_left; // Twist Left
            }

            // Digital Buttons
            {
               p_input->u8[ 0 ] = 0;

               if ( input_state_cb( iplayer, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP ) )
                  p_input->u8[ 0 ] |= ( 1 << 4 ); // Pad-Up
               if ( input_state_cb( iplayer, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT ) )
                  p_input->u8[ 0 ] |= ( 1 << 5 ); // Pad-Right
               if ( input_state_cb( iplayer, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN ) )
                  p_input->u8[ 0 ] |= ( 1 << 6 ); // Pad-Down
               if ( input_state_cb( iplayer, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT ) )
                  p_input->u8[ 0 ] |= ( 1 << 7 ); // Pad-Left
               if ( input_state_cb( iplayer, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START ) )
                  p_input->u8[ 0 ] |= ( 1 << 3 ); // Start

               p_input->u8[ 1 ] = 0;

               if ( input_state_cb( iplayer, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A ) )
                  p_input->u8[ 1 ] |= ( 1 << 5 ); // neGcon A
               if ( input_state_cb( iplayer, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X ) )
                  p_input->u8[ 1 ] |= ( 1 << 4 ); // neGcon B
               /*if ( input_state_cb( iplayer, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L ) )
                 p_input->u8[ 1 ] |= ( 1 << 2 ); // neGcon L shoulder (digital - non-standard?)
                 */
               if ( input_state_cb( iplayer, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R ) )
                  p_input->u8[ 1 ] |= ( 1 << 3 ); // neGcon R shoulder (digital)
               /*if ( input_state_cb( iplayer, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2 ) )
                 p_input->u8[ 1 ] |= ( 1 << 0 ); // neGcon L2 (non-standard?)
                 */
               /*if ( input_state_cb( iplayer, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2 ) )
                 p_input->u8[ 1 ] |= ( 1 << 1 ); // neGcon R2 (non-standard?)
                 */
            }

            break;

      } // switch ( input_type[ iplayer ] )


      //
      // -- Dual Analog Sticks

      switch ( input_type[ iplayer ] )
      {

         case RETRO_DEVICE_PS_DUALSHOCK:
         case RETRO_DEVICE_PS_ANALOG:
         case RETRO_DEVICE_PS_ANALOG_JOYSTICK:

            {
               int analog_left_x = input_state_cb( iplayer, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,
                     RETRO_DEVICE_ID_ANALOG_X);

               int analog_left_y = input_state_cb( iplayer, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,
                     RETRO_DEVICE_ID_ANALOG_Y);

               int analog_right_x = input_state_cb( iplayer, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT,
                     RETRO_DEVICE_ID_ANALOG_X);

               int analog_right_y = input_state_cb( iplayer, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT,
                     RETRO_DEVICE_ID_ANALOG_Y);

               struct analog_calibration *calibration = &analog_calibration[ iplayer ];

               uint32_t r_right = analog_right_x > 0 ?  analog_right_x : 0;
               uint32_t r_left  = analog_right_x < 0 ? -analog_right_x : 0;
               uint32_t r_down  = analog_right_y > 0 ?  analog_right_y : 0;
               uint32_t r_up    = analog_right_y < 0 ? -analog_right_y : 0;

               uint32_t l_right = analog_left_x > 0 ?  analog_left_x : 0;
               uint32_t l_left  = analog_left_x < 0 ? -analog_left_x : 0;
               uint32_t l_down  = analog_left_y > 0 ?  analog_left_y : 0;
               uint32_t l_up    = analog_left_y < 0 ? -analog_left_y : 0;

               if ( enable_analog_calibration )
               {
                  // Compute the "radius" (distance from 0, 0) of the current
                  // stick position, using the same normalized values
                  float l = analog_radius(analog_left_x, analog_left_y);
                  float r = analog_radius(analog_right_x, analog_right_y);

                  // We recalibrate when we find a new max value for the sticks
                  if ( l > analog_calibration->left )
                  {
                     analog_calibration->left = l;
                     log_cb(RETRO_LOG_DEBUG, "Recalibrating left stick, radius: %f\n", l);
                  }

                  if ( r > analog_calibration->right )
                  {
                     analog_calibration->right = r;
                     log_cb(RETRO_LOG_DEBUG, "Recalibrating right stick, radius: %f\n", r);
                  }

                  // This represents the maximal value the DualShock sticks can
                  // reach, where 1.0 would be the maximum value along the X or
                  // Y axis. XXX I need to measure this value more precisely,
                  // it's a rough estimate at the moment.
                  static const float dualshock_analog_radius = 1.35;

                  // Now compute the scaling factor to apply to convert the
                  // emulator's controller coordinates to a native DualShock's
                  // ones
                  float l_scaling = dualshock_analog_radius / analog_calibration->left;
                  float r_scaling = dualshock_analog_radius / analog_calibration->right;

                  analog_scale(&l_left, l_scaling);
                  analog_scale(&l_right, l_scaling);
                  analog_scale(&l_up, l_scaling);
                  analog_scale(&l_down, l_scaling);

                  analog_scale(&r_left, r_scaling);
                  analog_scale(&r_right, r_scaling);
                  analog_scale(&r_up, r_scaling);
                  analog_scale(&r_down, r_scaling);
               }
               else
               {
                  // Reset the calibration. Since we only increase the
                  // calibration coordinates we can start with a reasonably
                  // small value.
                  analog_calibration->left = 0.7;
                  analog_calibration->right = 0.7;
               }

               p_input->u32[1] = r_right;
               p_input->u32[2] = r_left;
               p_input->u32[3] = r_down;
               p_input->u32[4] = r_up;

               p_input->u32[5] = l_right;
               p_input->u32[6] = l_left;
               p_input->u32[7] = l_down;
               p_input->u32[8] = l_up;
            }

            break;

      } // switch ( input_type[ iplayer ] )


      //
      // -- Rumble

      if ( rumble.set_rumble_state )
      {
         switch ( input_type[ iplayer ] )
         {

            case RETRO_DEVICE_PS_DUALSHOCK:

               {
                  // Appears to be correct.
                  rumble.set_rumble_state( iplayer, RETRO_RUMBLE_WEAK, p_input->u8[9 * 4] * 0x101);
                  rumble.set_rumble_state( iplayer, RETRO_RUMBLE_STRONG, p_input->u8[9 * 4 + 1] * 0x101);

                  /*log_cb( RETRO_LOG_INFO, "Controller %u: Rumble: %d %d\n",
                    iplayer,
                    p_input->u8[9 * 4] * 0x101,
                    p_input->u8[9 * 4 + 1] * 0x101
                    );*/
               }

               break;

         } // switch ( input_type[ iplayer ] )

      } // can we rumble?

   } // for each player
}

//------------------------------------------------------------------------------
// Libretro Interface
//------------------------------------------------------------------------------

void retro_set_controller_port_device( unsigned in_port, unsigned device )
{
   if ( in_port < MAX_CONTROLLERS )
   {
      // Store input type
      input_type[ in_port ] = device;

      switch ( device )
      {

         case RETRO_DEVICE_NONE:
            log_cb( RETRO_LOG_INFO, "Controller %u: Unplugged\n", (in_port+1) );
            SetInput( in_port, "none", (uint8*)&input_data[ in_port ] );
            break;

         case RETRO_DEVICE_JOYPAD:
         case RETRO_DEVICE_PS_CONTROLLER:
            log_cb( RETRO_LOG_INFO, "Controller %u: PlayStation Controller\n", (in_port+1) );
            SetInput( in_port, "gamepad", (uint8*)&input_data[ in_port ] );
            break;

         case RETRO_DEVICE_PS_DUALSHOCK:
            log_cb( RETRO_LOG_INFO, "Controller %u: DualShock\n", (in_port+1) );
            SetInput( in_port, "dualshock", (uint8*)&input_data[ in_port ] );
            break;

         case RETRO_DEVICE_PS_ANALOG:
            log_cb( RETRO_LOG_INFO, "Controller %u: Analog Controller\n", (in_port+1) );
            SetInput( in_port, "dualanalog", (uint8*)&input_data[ in_port ] );
            break;

         case RETRO_DEVICE_PS_ANALOG_JOYSTICK:
            log_cb( RETRO_LOG_INFO, "Controller %u: Analog Joystick\n", (in_port+1) );
            SetInput( in_port, "analogjoy", (uint8*)&input_data[ in_port ] );
            break;

         case RETRO_DEVICE_PS_GUNCON:
            log_cb( RETRO_LOG_INFO, "Controller %u: Guncon / G-Con 45\n", (in_port+1) );
            SetInput( in_port, "guncon", (uint8*)&input_data[ in_port ] );
            if ( FIO )
               FIO->SetCrosshairsCursor( in_port, gun_cursor );
            break;

         case RETRO_DEVICE_PS_JUSTIFIER:
            log_cb( RETRO_LOG_INFO, "Controller %u: Justifier\n", (in_port+1) );
            SetInput( in_port, "justifier", (uint8*)&input_data[ in_port ] );
            if ( FIO )
               FIO->SetCrosshairsCursor( in_port, gun_cursor );
            break;

         case RETRO_DEVICE_PS_MOUSE:
            log_cb( RETRO_LOG_INFO, "Controller %u: Mouse\n", (in_port+1) );
            SetInput( in_port, "mouse", (uint8*)&input_data[ in_port ] );
            break;

         case RETRO_DEVICE_PS_NEGCON:
            log_cb( RETRO_LOG_INFO, "Controller %u: neGcon\n", (in_port+1) );
            SetInput( in_port, "negcon", (uint8*)&input_data[ in_port ] );
            break;

         default:
            log_cb( RETRO_LOG_WARN, "Controller %u: Unsupported Device (%u)\n", (in_port+1), device );
            SetInput( in_port, "none", (uint8*)&input_data[ in_port ] );
            break;

      } // switch ( device )

      // Clear rumble.
      if ( rumble.set_rumble_state )
      {
         rumble.set_rumble_state(in_port, RETRO_RUMBLE_STRONG, 0);
         rumble.set_rumble_state(in_port, RETRO_RUMBLE_WEAK, 0);
      }
      input_data[ in_port ].u32[ 9 ] = 0;

   } // valid port?
}

//==============================================================================
