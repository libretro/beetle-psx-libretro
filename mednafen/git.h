#ifndef _GIT_H
#define _GIT_H

/* Mednafen "Game Information" header - originally a sprawling collection
 * of types describing emulated systems, video modes, input devices, and
 * cheat formats for Mednafen's own GUI. This libretro core re-uses only
 * a small fraction:
 *
 *   - MDFN_MSC_*                  : DoSimpleCommand opcodes
 *   - EmulateSpecStruct           : per-frame emulation state passed to
 *                                   GPU/SPU/etc.
 *   - CheatFormatStruct           : decoder vtable for retro_cheat_set
 *   - struct MemoryPatch (fwd)    : referenced by CheatFormatStruct
 *
 * Everything else (MDFNGI, InputInfoStruct, VideoSystems, ModPrio,
 * GameMediumTypes, MDFN_ROTATE*) was metadata for Mednafen's standalone
 * UI and never read by the libretro driver, so it was removed during
 * the dead-code audit.
 *
 * Now plain C - originally CheatFormatStruct's DecodeCheat callback
 * took `const std::string&` for the cheat string, which trapped this
 * whole header behind C++. The single call site (libretro.cpp's
 * retro_cheat_set) was already constructing a temporary std::string
 * from a C string just to call into DecodeGS, which then read it
 * character-by-character via std::string::operator[] and .size().
 * Switching to `const char *` (with strlen) is shorter at every layer
 * and frees this header (and everything that transitively includes
 * it - notably mednafen/psx/gpu.h) for inclusion from C TUs. */

#include <libretro.h>

#include "video/surface.h"
#include "state.h"

#ifdef __cplusplus
extern "C" {
#endif

struct MemoryPatch;

typedef struct
{
   const char *FullName;
   const char *Description;

   bool (*DecodeCheat)(const char *cheat_string, struct MemoryPatch *patch);
} CheatFormatStruct;

/* DoSimpleCommand opcodes. Originally a much larger enum from
 * Mednafen covering coin slots, DIP switches, and per-disk
 * insertion shortcuts for arcade and multi-disk console targets;
 * trimmed to only what the PSX core's DoSimpleCommand actually
 * dispatches on. */
enum
{
   MDFN_MSC_RESET       = 0x01,
   MDFN_MSC_POWER       = 0x02,

   MDFN_MSC_INSERT_DISK = 0x30,
   MDFN_MSC_EJECT_DISK  = 0x31,
   MDFN_MSC_SELECT_DISK = 0x32
};

typedef struct
{
   /* Pitch (32-bit) must be equal to width and >= the framebuffer width
    * for the emulated system. The framebuffer pointed to by
    * surface->pixels is written to by the system emulation code. */
   MDFN_Surface *surface;

   /* Set by the system emulation code every frame, to denote the
    * horizontal and vertical offsets of the image, and the size of the
    * image. If the emulated system sets the elements of LineWidths,
    * then the horizontal offset(x) and width(w) of this structure are
    * ignored while drawing the image. */
   MDFN_Rect DisplayRect;

   /* Pointer to an array of MDFN_Rect, set by the driver code.
    * Individual MDFN_Rect structs written to by system emulation code. */
   int32_t *LineWidths;

   /* Set (optionally) by emulation code. If InterlaceOn is true, then
    * assume field height is 1/2 DisplayRect.h, and only every other
    * line in surface (with the start line defined by InterlacedField)
    * has valid data. */
   bool InterlaceOn;
   bool InterlaceField;

   /* Skip rendering this frame if true. Set by the driver code. */
   int skip;

   /* Sound rate. Set by driver side. */
   double SoundRate;

   /* Number of frames currently in internal sound buffer. Set by the
    * system emulation code, to be read by the driver code. */
   int32_t SoundBufSize;
} EmulateSpecStruct;

extern retro_log_printf_t log_cb;

#ifdef __cplusplus
}
#endif

#endif
