#ifndef __MDFN_PSX_FRONTIO_H
#define __MDFN_PSX_FRONTIO_H

#include <stdint.h>
#include <boolean.h>

#include "../state_helpers.h"
#include "../state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Crosshair-cursor selector values, formerly the FrontIO::SETTING_GUN_*
 * enum.  Promoted to file scope so both the InputDevice base struct
 * and external callers (e.g. libretro.cpp) can reference them without
 * pulling in C++ class scoping. */
enum
{
   SETTING_GUN_CROSSHAIR_OFF   = 0,
   SETTING_GUN_CROSSHAIR_CROSS = 1,
   SETTING_GUN_CROSSHAIR_DOT   = 2,

   SETTING_GUN_CROSSHAIR_LAST
};

/* Forward decls.  InputDevice is the polymorphic base; concrete
 * device types (gamepad, dualshock, multitap, ...) are defined
 * file-locally inside frontio.cpp.  External callers only ever hold
 * an InputDevice * and dispatch through the vtable. */
typedef struct InputDevice          InputDevice;
typedef struct InputDevice_VTable   InputDevice_VTable;

/* Shared state for every InputDevice subclass: crosshair colour,
 * crosshair-cursor selector, and lightgun-derived screen position.
 *
 * Was previously the private/protected fields of class InputDevice;
 * now exposed inline because every concrete-device struct embeds an
 * InputDevice as its first member and writes through these fields
 * directly via the vtable methods. */
struct InputDevice
{
   const InputDevice_VTable *vt;

   unsigned chair_r;
   unsigned chair_g;
   unsigned chair_b;
   int      chair_cursor;
   int32_t  chair_x;
   int32_t  chair_y;
};

/* Per-device dispatch table.  Each concrete device type has a single
 * static const InputDevice_VTable instance and points its embedded
 * InputDevice::vt at it.  All polymorphic calls go through
 * (dev->vt->Method)(dev, args).  Reference parameters in the original
 * C++ (e.g. int32_t &) become pointers in C - see Clock below. */
struct InputDevice_VTable
{
   void     (*Power)            (InputDevice *self_);
   void     (*UpdateInput)      (InputDevice *self_, const void *data);
   int      (*StateAction)      (InputDevice *self_, StateMem *sm,
                                 int load, int data_only,
                                 const char *section_name);
   int32_t  (*GPULineHook)      (InputDevice *self_,
                                 const int32_t line_timestamp,
                                 bool vsync, uint32_t *pixels,
                                 const unsigned width,
                                 const unsigned pix_clock_offset,
                                 const unsigned pix_clock,
                                 const unsigned pix_clock_divider,
                                 const unsigned surf_pitchinpix,
                                 const unsigned upscale_factor);
   void     (*Update)           (InputDevice *self_, const int32_t timestamp);
   void     (*ResetTS)          (InputDevice *self_);
   void     (*SetAMCT)          (InputDevice *self_, bool enabled);
   void     (*SetCrosshairsCursor)(InputDevice *self_, int cursor);
   void     (*SetCrosshairsColor) (InputDevice *self_, uint32_t color);
   void     (*SetDTR)           (InputDevice *self_, bool new_dtr);
   bool     (*GetDSR)           (InputDevice *self_);
   bool     (*Clock)            (InputDevice *self_, bool TxD,
                                 int32_t *dsr_pulse_delay);
   uint8_t *(*GetNVData)        (InputDevice *self_);
   uint32_t (*GetNVSize)        (InputDevice *self_);
   void     (*ReadNV)           (InputDevice *self_, uint8_t *buffer,
                                 uint32_t offset, uint32_t count);
   void     (*WriteNV)          (InputDevice *self_, const uint8_t *buffer,
                                 uint32_t offset, uint32_t count);
   uint64_t (*GetNVDirtyCount)  (InputDevice *self_);
   void     (*ResetNVDirtyCount)(InputDevice *self_);

   /* Per-device dtor.  Frees any dynamically-allocated state owned
    * by the device.  Caller frees the InputDevice block itself
    * afterwards.  NULL when the device holds no extra state. */
   void     (*Destroy)          (InputDevice *self_);
};

/* The non-virtual InputDevice::DrawCrosshairs.  Free function with
 * the InputDevice * passed explicitly. */
void InputDevice_DrawCrosshairs(InputDevice *self_, uint32_t *pixels,
                                const unsigned width,
                                const unsigned pix_clock,
                                const unsigned surf_pitchinpix,
                                const unsigned upscale_factor);

/* FrontIO is fully opaque to external callers - the struct is
 * defined inside frontio.cpp and only ever touched through the
 * functions below. */
typedef struct FrontIO FrontIO;

FrontIO *FrontIO_New(bool emulate_memcards[8], bool emulate_multitap[2]);
void     FrontIO_Free(FrontIO *fio);

void     FrontIO_Power(FrontIO *fio);
void     FrontIO_Write(FrontIO *fio, int32_t timestamp,
                       uint32_t A, uint32_t V);
uint32_t FrontIO_Read (FrontIO *fio, int32_t timestamp, uint32_t A);
int32_t  FrontIO_CalcNextEventTS(FrontIO *fio, int32_t timestamp,
                                 int32_t next_event);
int32_t  FrontIO_Update(FrontIO *fio, int32_t timestamp);
void     FrontIO_ResetTS(FrontIO *fio);

void     FrontIO_GPULineHook(FrontIO *fio,
                             const int32_t timestamp,
                             const int32_t line_timestamp,
                             bool vsync, uint32_t *pixels,
                             const unsigned width,
                             const unsigned pix_clock_offset,
                             const unsigned pix_clock,
                             const unsigned pix_clock_divider,
                             const unsigned surf_pitchinpix,
                             const unsigned upscale_factor);

void     FrontIO_UpdateInput(FrontIO *fio);
void     FrontIO_SetInput(FrontIO *fio, unsigned port,
                          const char *type, void *ptr);
void     FrontIO_SetAMCT(FrontIO *fio, bool enabled);
void     FrontIO_SetCrosshairsCursor(FrontIO *fio, unsigned port, int cursor);
void     FrontIO_SetCrosshairsColor (FrontIO *fio, unsigned port, uint32_t color);

InputDevice *FrontIO_GetMemcardDevice(FrontIO *fio, unsigned which);
uint64_t     FrontIO_GetMemcardDirtyCount(FrontIO *fio, unsigned which);
/* The C++ overloads of LoadMemcard / SaveMemcard collapse to two
 * functions each: the (which, path, force_load) form is the
 * primitive; the (which) form picks up the path from the saved
 * mc_filenames[] cache and is implemented as a thin wrapper. */
void     FrontIO_LoadMemcardFromPath(FrontIO *fio, unsigned which,
                                     const char *path, bool force_load);
void     FrontIO_LoadMemcard         (FrontIO *fio, unsigned which);
void     FrontIO_SaveMemcardToPath  (FrontIO *fio, unsigned which,
                                     const char *path, bool force_save);
void     FrontIO_SaveMemcard         (FrontIO *fio, unsigned which);

int      FrontIO_StateAction(FrontIO *fio, StateMem *sm,
                             int load, int data_only);

#ifdef __cplusplus
}
#endif

#endif /* __MDFN_PSX_FRONTIO_H */
