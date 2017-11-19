
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
	uint8_t u8[ 10 * sizeof( uint32_t ) ];
	uint32_t u32[ 10 ]; /* 1 + 8 + 1 */
	uint16_t buttons;
}
INPUT_DATA;

// Controller state buffer (per player)
static INPUT_DATA input_data[ MAX_CONTROLLERS ] = {0};

struct analog_calibration
{
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

enum { INPUT_DEVICE_TYPES_COUNT = 1 /*none*/ + 5 }; // <-- update me!

static const struct retro_controller_description input_device_types[ INPUT_DEVICE_TYPES_COUNT ] =
{
	{ "PlayStation Controller", RETRO_DEVICE_JOYPAD },
	{ "DualShock", RETRO_DEVICE_PS_DUALSHOCK },
	{ "Analog Controller", RETRO_DEVICE_PS_ANALOG },
	{ "Analog Joystick", RETRO_DEVICE_PS_ANALOG_JOYSTICK },
	{ "Mouse", RETRO_DEVICE_PS_MOUSE },
	{ NULL, 0 },
};



//------------------------------------------------------------------------------
// Mapping Helpers
//------------------------------------------------------------------------------

/* Controller (default) */
enum { INPUT_MAP_CONTROLLER_SIZE = 16 };
static const unsigned input_map_controller[ INPUT_MAP_CONTROLLER_SIZE ] =
{
	// libretro input				 at position	|| maps to PS1			on bit
	//-----------------------------------------------------------------------------
#ifdef MSB_FIRST
	RETRO_DEVICE_ID_JOYPAD_L2,		// L-trigger	-> L2
	RETRO_DEVICE_ID_JOYPAD_R2,		// R-trigger	-> R2
	RETRO_DEVICE_ID_JOYPAD_L,		// L-shoulder	-> L1
	RETRO_DEVICE_ID_JOYPAD_R,		// R-shoulder	-> R1
	RETRO_DEVICE_ID_JOYPAD_X,		// X(top)		-> Triangle
	RETRO_DEVICE_ID_JOYPAD_A,		// A(right)		-> Circle
	RETRO_DEVICE_ID_JOYPAD_B,		// B(down)		-> Cross
	RETRO_DEVICE_ID_JOYPAD_Y,		// Y(left)		-> Square
	RETRO_DEVICE_ID_JOYPAD_SELECT,	// Select		-> Select
	RETRO_DEVICE_ID_JOYPAD_L3,		// L-thumb		-> L3
	RETRO_DEVICE_ID_JOYPAD_R3,		// R-thumb		-> R3
	RETRO_DEVICE_ID_JOYPAD_START,	// Start		-> Start
	RETRO_DEVICE_ID_JOYPAD_UP,		// Pad-Up		-> Pad-Up
	RETRO_DEVICE_ID_JOYPAD_RIGHT,	// Pad-Down		-> Pad-Down
	RETRO_DEVICE_ID_JOYPAD_DOWN,	// Pad-Left		-> Pad-Left
	RETRO_DEVICE_ID_JOYPAD_LEFT,	// Pad-Right	-> Pad-Right
#else
	RETRO_DEVICE_ID_JOYPAD_SELECT,	// Select		-> Select				0
	RETRO_DEVICE_ID_JOYPAD_L3,		// L-thumb		-> L3					1
	RETRO_DEVICE_ID_JOYPAD_R3,		// R-thumb		-> R3					2
	RETRO_DEVICE_ID_JOYPAD_START,	// Start		-> Start				3
	RETRO_DEVICE_ID_JOYPAD_UP,		// Pad-Up		-> Pad-Up				4
	RETRO_DEVICE_ID_JOYPAD_RIGHT,	// Pad-Down		-> Pad-Down				5
	RETRO_DEVICE_ID_JOYPAD_DOWN,	// Pad-Left		-> Pad-Left				6
	RETRO_DEVICE_ID_JOYPAD_LEFT,	// Pad-Right	-> Pad-Right			7
	RETRO_DEVICE_ID_JOYPAD_L2,		// L-trigger	-> L2					8
	RETRO_DEVICE_ID_JOYPAD_R2,		// R-trigger	-> R2					9
	RETRO_DEVICE_ID_JOYPAD_L,		// L-shoulder	-> L1					10
	RETRO_DEVICE_ID_JOYPAD_R,		// R-shoulder	-> R1					11
	RETRO_DEVICE_ID_JOYPAD_X,		// X(top)		-> Triangle				12
	RETRO_DEVICE_ID_JOYPAD_A,		// A(right)		-> Circle				13
	RETRO_DEVICE_ID_JOYPAD_B,		// B(down)		-> Cross				14
	RETRO_DEVICE_ID_JOYPAD_Y,		// Y(left)		-> Square				15
#endif
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
	// For each player (logical controller)
	for ( unsigned iplayer = 0; iplayer < players; ++iplayer )
	{
		INPUT_DATA* p_input = &(input_data[ iplayer ]);

		//
		// -- Buttons

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
			for ( unsigned i = 0; i < INPUT_MAP_CONTROLLER_SIZE; i++ )
			{
				p_input->buttons |=
					input_state_cb( iplayer, RETRO_DEVICE_JOYPAD, 0, input_map_controller[ i ] )
						? ( 1 << i ) : 0;
			}

			break;

		case RETRO_DEVICE_PS_MOUSE:

			// Simple two-button mouse.
			{
				p_input->u32[ 2 ] = 0;
				if ( input_state_cb( iplayer, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT ) ) {
					p_input->u32[ 2 ] |= ( 1 << 1 ); // Left
				}
				if ( input_state_cb( iplayer, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT ) ) {
					p_input->u32[ 2 ] |= ( 1 << 0 ); // Right
				}
			}

			// Relative input.
			{
				// mouse input
				int dx_raw, dy_raw;
				dx_raw = input_state_cb( iplayer, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X );
				dy_raw = input_state_cb( iplayer, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y );

				p_input->u32[ 0 ] = dx_raw;
				p_input->u32[ 1 ] = dy_raw;
			}

			break;

		}; // switch ( input_type[ iplayer ] )


		//
		// -- Analog Sticks

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
					if ( l > analog_calibration->left ) {
						analog_calibration->left = l;
						log_cb(RETRO_LOG_DEBUG, "Recalibrating left stick, radius: %f\n", l);
					}

					if ( r > analog_calibration->right ) {
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

		}; // switch ( input_type[ iplayer ] )


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

			}; // switch ( input_type[ iplayer ] )

		}; // can we rumble?

	}; // for each player
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

		case RETRO_DEVICE_PS_MOUSE:
			log_cb( RETRO_LOG_INFO, "Controller %u: Mouse\n", (in_port+1) );
			SetInput( in_port, "mouse", (uint8*)&input_data[ in_port ] );
			break;

		default:
			log_cb( RETRO_LOG_WARN, "Controller %u: Unsupported Device (%u)\n", (in_port+1), device );
			SetInput( in_port, "none", (uint8*)&input_data[ in_port ] );
			break;

		}; // switch ( device )

		// Clear rumble.
		if ( rumble.set_rumble_state )
		{
			rumble.set_rumble_state(in_port, RETRO_RUMBLE_STRONG, 0);
			rumble.set_rumble_state(in_port, RETRO_RUMBLE_WEAK, 0);
		}
		input_data[ in_port ].u32[ 9 ] = 0;

	}; // valid port?
}

//==============================================================================
