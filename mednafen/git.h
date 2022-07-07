#ifndef _GIT_H
#define _GIT_H

#include <libretro.h>

#include "video/surface.h"
#include "state.h"

enum
{
   MDFN_ROTATE0 = 0,
   MDFN_ROTATE90,
   MDFN_ROTATE180,
   MDFN_ROTATE270
};

typedef enum
{
   VIDSYS_NONE,
   VIDSYS_PAL,
   VIDSYS_PAL_M,
   VIDSYS_NTSC,
   VIDSYS_SECAM
} VideoSystems;

typedef enum
{
   GMT_CART,
   GMT_ARCADE,
   GMT_DISK,
   GMT_CDROM,
   GMT_PLAYER
} GameMediumTypes;

typedef enum
{
   IDIT_BUTTON,
   IDIT_BUTTON_CAN_RAPID,

   IDIT_X_AXIS,
   IDIT_Y_AXIS,
   IDIT_X_AXIS_REL,
   IDIT_Y_AXIS_REL,

   IDIT_BYTE_SPECIAL,

   IDIT_BUTTON_ANALOG,
   IDIT_RUMBLE
} InputDeviceInputType;

#define IDIT_BUTTON_ANALOG_FLAG_SQLR	0x00000001

typedef struct
{
   const char *SettingName;
   const char *Name;
   const int ConfigOrder;
   const InputDeviceInputType Type;
   const char *ExcludeName;
   const char *RotateName[3];
   unsigned Flags;
} InputDeviceInputInfoStruct;

typedef struct
{
   const char *ShortName;
   const char *FullName;
   const char *Description;

   const void *PortExpanderDeviceInfo;
   int NumInputs;
   const InputDeviceInputInfoStruct *IDII;
} InputDeviceInfoStruct;

typedef struct
{
   const char *ShortName;
   const char *FullName;
   int NumTypes;
   InputDeviceInfoStruct *DeviceInfo;
   const char *DefaultDevice;
} InputPortInfoStruct;

typedef struct
{
   int InputPorts;
   const InputPortInfoStruct *Types;
} InputInfoStruct;

struct MemoryPatch;

struct CheatFormatStruct
{
   const char *FullName;
   const char *Description;

   bool (*DecodeCheat)(const std::string& cheat_string, MemoryPatch* patch);
};

struct CheatFormatInfoStruct
{
   unsigned NumFormats;

   CheatFormatStruct *Formats;
};

enum
{
   MDFN_MSC_RESET = 0x01,
   MDFN_MSC_POWER = 0x02,

   MDFN_MSC_INSERT_COIN = 0x07,

   MDFN_MSC_TOGGLE_DIP0 = 0x10,
   MDFN_MSC_TOGGLE_DIP1,
   MDFN_MSC_TOGGLE_DIP2,
   MDFN_MSC_TOGGLE_DIP3,
   MDFN_MSC_TOGGLE_DIP4,
   MDFN_MSC_TOGGLE_DIP5,
   MDFN_MSC_TOGGLE_DIP6,
   MDFN_MSC_TOGGLE_DIP7,
   MDFN_MSC_TOGGLE_DIP8,
   MDFN_MSC_TOGGLE_DIP9,
   MDFN_MSC_TOGGLE_DIP10,
   MDFN_MSC_TOGGLE_DIP11,
   MDFN_MSC_TOGGLE_DIP12,
   MDFN_MSC_TOGGLE_DIP13,
   MDFN_MSC_TOGGLE_DIP14,
   MDFN_MSC_TOGGLE_DIP15,


   // n of DISKn translates to is emulation module specific.
   MDFN_MSC_INSERT_DISK0 = 0x20,
   MDFN_MSC_INSERT_DISK1,
   MDFN_MSC_INSERT_DISK2,
   MDFN_MSC_INSERT_DISK3,
   MDFN_MSC_INSERT_DISK4,
   MDFN_MSC_INSERT_DISK5,
   MDFN_MSC_INSERT_DISK6,
   MDFN_MSC_INSERT_DISK7,
   MDFN_MSC_INSERT_DISK8,
   MDFN_MSC_INSERT_DISK9,
   MDFN_MSC_INSERT_DISK10,
   MDFN_MSC_INSERT_DISK11,
   MDFN_MSC_INSERT_DISK12,
   MDFN_MSC_INSERT_DISK13,
   MDFN_MSC_INSERT_DISK14,
   MDFN_MSC_INSERT_DISK15,

   MDFN_MSC_INSERT_DISK	= 0x30,
   MDFN_MSC_EJECT_DISK 	= 0x31,

   MDFN_MSC_SELECT_DISK	= 0x32,

   MDFN_MSC__LAST = 0x3F
};

typedef struct
{
   // Pitch(32-bit) must be equal to width and >= the "fb_width" specified in the MDFNGI struct for the emulated system.
   // Height must be >= to the "fb_height" specified in the MDFNGI struct for the emulated system.
   // The framebuffer pointed to by surface->pixels is written to by the system emulation code.
   MDFN_Surface *surface;

   // Will be set to TRUE if the video pixel format has changed since the last call to Emulate(), FALSE otherwise.
   // Will be set to TRUE on the first call to the Emulate() function/method
   bool VideoFormatChanged;

   // Set by the system emulation code every frame, to denote the horizontal and vertical offsets of the image, and the size
   // of the image.  If the emulated system sets the elements of LineWidths, then the horizontal offset(x) and width(w) of this structure
   // are ignored while drawing the image.
   MDFN_Rect DisplayRect;

   // Pointer to an array of MDFN_Rect, number of elements = fb_height, set by the driver code.  Individual MDFN_Rect structs written
   // to by system emulation code.  If the emulated system doesn't support multiple screen widths per frame, or if you handle
   // such a situation by outputting at a constant width-per-frame that is the least-common-multiple of the screen widths, then
   // you can ignore this.  If you do wish to use this, you must set all elements every frame.
   int32_t *LineWidths;

   // Set(optionally) by emulation code.  If InterlaceOn is true, then assume field height is 1/2 DisplayRect.h, and
   // only every other line in surface (with the start line defined by InterlacedField) has valid data
   // (it's up to internal Mednafen code to deinterlace it).
   bool InterlaceOn;
   bool InterlaceField;

   // Skip rendering this frame if true.  Set by the driver code.
   int skip;

   // Sound rate.  Set by driver side.
   double SoundRate;

   // Number of frames currently in internal sound buffer.  Set by the system emulation code, to be read by the driver code.
   int32_t SoundBufSize;
} EmulateSpecStruct;

typedef enum
{
   MODPRIO_INTERNAL_EXTRA_LOW = 0,	// For "cdplay" module, mostly.

   MODPRIO_INTERNAL_LOW = 10,
   MODPRIO_EXTERNAL_LOW = 20,
   MODPRIO_INTERNAL_HIGH = 30,
   MODPRIO_EXTERNAL_HIGH = 40
} ModPrio;

class CDIF;

// Time base for EmulateSpecStruct::MasterCycles
#define MDFN_MASTERCLOCK_FIXED(n)	((int64_t)((double)(n) * (INT64_C(1) << 32)))

typedef struct
{
   // multires is a hint that, if set, indicates that the system has fairly programmable video modes(particularly, the ability
   // to display multiple horizontal resolutions, such as the PCE, PC-FX, or Genesis).  In practice, it will cause the driver
   // code to set the linear interpolation on by default.
   //
   // lcm_width and lcm_height are the least common multiples of all possible
   // resolutions in the frame buffer as specified by DisplayRect/LineWidths(Ex for PCE: widths of 256, 341.333333, 512,
   // lcm = 1024)
   //
   // nominal_width and nominal_height specify the resolution that Mednafen should display
   // the framebuffer image in at 1x scaling, scaled from the dimensions of DisplayRect, and optionally the LineWidths array
   // passed through espec to the Emulate() function.
   //
   bool multires;

   int lcm_width;
   int lcm_height;

   void *dummy_separator;	//

   int nominal_width;
   int nominal_height;

   int fb_width;		// Width of the framebuffer(not necessarily width of the image).  MDFN_Surface width should be >= this.
   int fb_height;		// Height of the framebuffer passed to the Emulate() function(not necessarily height of the image)

   GameMediumTypes GameType;

   // For absolute coordinates(IDIT_X_AXIS and IDIT_Y_AXIS), usually mapped to a mouse(hence the naming).
   float mouse_scale_x, mouse_scale_y;
   float mouse_offs_x, mouse_offs_y;
} MDFNGI;

extern retro_log_printf_t log_cb;

#endif
