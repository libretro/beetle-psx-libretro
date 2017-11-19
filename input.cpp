
#include "libretro.h"
#include "mednafen/mednafen-types.h"
#include <math.h>
#include <stdio.h>
#include "mednafen/git.h"
#include "mednafen/psx/frontio.h"

//------------------------------------------------------------------------------
// Locals
//------------------------------------------------------------------------------

static retro_environment_t environ_cb; // cached during input_set_env
static retro_rumble_interface rumble; // acquired during input_init_env

static FrontIO* FIO; // cached in input_set_fio

#define MAX_CONTROLLERS 8

static unsigned players = 2;
static bool enable_analog_calibration = false;

typedef union
{
	uint8_t u8[ 10 * sizeof(uint32_t) ];
	uint8_t u32[ 10 ];
}
INPUT_DATA;

// Controller state buffer (per player)
static INPUT_DATA input_data[ MAX_CONTROLLERS ] = {0};

union
{
   uint32_t u32[MAX_CONTROLLERS][1 + 8 + 1]; // Buttons + Axes + Rumble
   uint8_t u8[MAX_CONTROLLERS][(1 + 8 + 1) * sizeof(uint32_t)];
} static buf;
#define MAX_BUTTONS 16

static uint16_t input_buf[MAX_CONTROLLERS] = {0};

struct analog_calibration {
  float left;
  float right;
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
#define RETRO_DEVICE_PS_MOUSE              RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_MOUSE, 0)

enum { INPUT_DEVICE_TYPES_COUNT = 1 /*none*/ + 4 }; // <-- update me!

static const struct retro_controller_description input_device_types[ INPUT_DEVICE_TYPES_COUNT ] =
{
	{ "PlayStation Controller", RETRO_DEVICE_JOYPAD },
	{ "DualShock", RETRO_DEVICE_PS_DUALSHOCK },
	{ "Analog Controller", RETRO_DEVICE_PS_ANALOG },
	{ "Analog Joystick", RETRO_DEVICE_PS_ANALOG_JOYSTICK },
	{ NULL, 0 },
};


//------------------------------------------------------------------------------
// Local Functions
//------------------------------------------------------------------------------

// Return the normalized distance between the origin and the current
// (x,y) analog stick position
static float analog_radius(int x, int y) {
  float fx = ((float)x) / 0x8000;
  float fy = ((float)y) / 0x8000;

  return sqrtf(fx * fx + fy * fy);
}

static void analog_scale(uint32_t *v, float s) {
  *v *= s;

  if (*v > 0x7fff) {
    *v = 0x7fff;
  }
}

static void SetInput(int port, const char *type, void *ptr)
{
   FIO->SetInput(port, type, ptr);
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
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,    "Select" },
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
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,    "Select" },
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
		{ 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,    "Select" },
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
		{ 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,    "Select" },
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
		{ 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,    "Select" },
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
		{ 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,    "Select" },
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
		{ 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,    "Select" },
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
		{ 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,    "Select" },
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

void input_set_env( retro_environment_t environ_cb )
{
	static const struct retro_controller_info ports[ MAX_CONTROLLERS + 1 ] =
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

	environ_cb( RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports );
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
	}
}

void input_enable_calibration( bool enable )
{
	enable_analog_calibration = enable;
}

void input_set_player_count( unsigned _players )
{
	players = _players;
}

unsigned input_get_player_count()
{
	return players;
}

void input_update( retro_input_state_t input_state_cb )
{
   //input_buf[0] = 0;
   //input_buf[1] = 0;

   for (unsigned j = 0; j < players; j++)
   {
       input_buf[j] = 0;
   }

   static unsigned map[] = {
#ifdef MSB_FIRST
      RETRO_DEVICE_ID_JOYPAD_L2,
      RETRO_DEVICE_ID_JOYPAD_R2,
      RETRO_DEVICE_ID_JOYPAD_L,
      RETRO_DEVICE_ID_JOYPAD_R,
      RETRO_DEVICE_ID_JOYPAD_X,
      RETRO_DEVICE_ID_JOYPAD_A,
      RETRO_DEVICE_ID_JOYPAD_B,
      RETRO_DEVICE_ID_JOYPAD_Y,
      RETRO_DEVICE_ID_JOYPAD_SELECT,
      RETRO_DEVICE_ID_JOYPAD_L3,
      RETRO_DEVICE_ID_JOYPAD_R3,
      RETRO_DEVICE_ID_JOYPAD_START,
      RETRO_DEVICE_ID_JOYPAD_UP,
      RETRO_DEVICE_ID_JOYPAD_RIGHT,
      RETRO_DEVICE_ID_JOYPAD_DOWN,
      RETRO_DEVICE_ID_JOYPAD_LEFT,
#else
      RETRO_DEVICE_ID_JOYPAD_SELECT,
      RETRO_DEVICE_ID_JOYPAD_L3,
      RETRO_DEVICE_ID_JOYPAD_R3,
      RETRO_DEVICE_ID_JOYPAD_START,
      RETRO_DEVICE_ID_JOYPAD_UP,
      RETRO_DEVICE_ID_JOYPAD_RIGHT,
      RETRO_DEVICE_ID_JOYPAD_DOWN,
      RETRO_DEVICE_ID_JOYPAD_LEFT,
      RETRO_DEVICE_ID_JOYPAD_L2,
      RETRO_DEVICE_ID_JOYPAD_R2,
      RETRO_DEVICE_ID_JOYPAD_L,
      RETRO_DEVICE_ID_JOYPAD_R,
      RETRO_DEVICE_ID_JOYPAD_X,
      RETRO_DEVICE_ID_JOYPAD_A,
      RETRO_DEVICE_ID_JOYPAD_B,
      RETRO_DEVICE_ID_JOYPAD_Y,
#endif
   };

   for (unsigned j = 0; j < players; j++)
   {
      for (unsigned i = 0; i < MAX_BUTTONS; i++)
         input_buf[j] |= input_state_cb(j, RETRO_DEVICE_JOYPAD, 0, map[i]) ? (1 << i) : 0;
   }

   // Buttons.
   //buf.u8[0][0] = (input_buf[0] >> 0) & 0xff;
   //buf.u8[0][1] = (input_buf[0] >> 8) & 0xff;
   //buf.u8[1][0] = (input_buf[1] >> 0) & 0xff;
   //buf.u8[1][1] = (input_buf[1] >> 8) & 0xff;

   for (unsigned j = 0; j < players; j++)
   {
        buf.u8[j][0] = (input_buf[j] >> 0) & 0xff;
        buf.u8[j][1] = (input_buf[j] >> 8) & 0xff;
   }

   // Analogs
   for (unsigned j = 0; j < players; j++)
   {
      int analog_left_x = input_state_cb(j, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,
            RETRO_DEVICE_ID_ANALOG_X);

      int analog_left_y = input_state_cb(j, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,
            RETRO_DEVICE_ID_ANALOG_Y);

      int analog_right_x = input_state_cb(j, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT,
            RETRO_DEVICE_ID_ANALOG_X);

      int analog_right_y = input_state_cb(j, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT,
            RETRO_DEVICE_ID_ANALOG_Y);

      struct analog_calibration *calibration = &analog_calibration[j];

      uint32_t r_right = analog_right_x > 0 ?  analog_right_x : 0;
      uint32_t r_left  = analog_right_x < 0 ? -analog_right_x : 0;
      uint32_t r_down  = analog_right_y > 0 ?  analog_right_y : 0;
      uint32_t r_up    = analog_right_y < 0 ? -analog_right_y : 0;

      uint32_t l_right = analog_left_x > 0 ?  analog_left_x : 0;
      uint32_t l_left  = analog_left_x < 0 ? -analog_left_x : 0;
      uint32_t l_down  = analog_left_y > 0 ?  analog_left_y : 0;
      uint32_t l_up    = analog_left_y < 0 ? -analog_left_y : 0;

      if (enable_analog_calibration) {
         // Compute the "radius" (distance from 0, 0) of the current
         // stick position, using the same normalized values
         float l = analog_radius(analog_left_x, analog_left_y);
         float r = analog_radius(analog_right_x, analog_right_y);

         // We recalibrate when we find a new max value for the sticks
         if (l > analog_calibration->left) {
            analog_calibration->left = l;
            log_cb(RETRO_LOG_DEBUG, "Recalibrating left stick, radius: %f\n", l);
         }

         if (r > analog_calibration->right) {
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
      } else {
         // Reset the calibration. Since we only increase the
         // calibration coordinates we can start with a reasonably
         // small value.
         analog_calibration->left = 0.7;
         analog_calibration->right = 0.7;
      }

      buf.u32[j][1] = r_right;
      buf.u32[j][2] = r_left;
      buf.u32[j][3] = r_down;
      buf.u32[j][4] = r_up;

      buf.u32[j][5] = l_right;
      buf.u32[j][6] = l_left;
      buf.u32[j][7] = l_down;
      buf.u32[j][8] = l_up;
   }

   //fprintf(stderr, "Rumble strong: %u, weak: %u.\n", buf.u8[0][9 * 4 + 1], buf.u8[0][9 * 4]);
   if (rumble.set_rumble_state)
   {
      // Appears to be correct.
      //rumble.set_rumble_state(0, RETRO_RUMBLE_WEAK, buf.u8[0][9 * 4] * 0x101);
      //rumble.set_rumble_state(0, RETRO_RUMBLE_STRONG, buf.u8[0][9 * 4 + 1] * 0x101);
      //rumble.set_rumble_state(1, RETRO_RUMBLE_WEAK, buf.u8[1][9 * 4] * 0x101);
      //rumble.set_rumble_state(1, RETRO_RUMBLE_STRONG, buf.u8[1][9 * 4 + 1] * 0x101);

      for (unsigned j = 0; j < players; j++)
      {
          rumble.set_rumble_state(j, RETRO_RUMBLE_WEAK, buf.u8[j][9 * 4] * 0x101);
          rumble.set_rumble_state(j, RETRO_RUMBLE_STRONG, buf.u8[j][9 * 4 + 1] * 0x101);
      }
   }
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

		default:
			log_cb( RETRO_LOG_WARN, "Controller %u: Unsupported Device (%u)\n", (in_port+1), device );
			SetInput( in_port, "none", (uint8*)&input_data[ in_port ] );
			break;

		}; // switch ( device )

		if ( rumble.set_rumble_state )
		{
			rumble.set_rumble_state(in_port, RETRO_RUMBLE_STRONG, 0);
			rumble.set_rumble_state(in_port, RETRO_RUMBLE_WEAK, 0);
			input_data[ in_port ].u32[ 9 ] = 0;
		}

	}; // valid port?
}

//==============================================================================
