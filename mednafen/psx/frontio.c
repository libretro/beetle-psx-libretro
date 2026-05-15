/* Mednafen - Multi-system Emulator
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <compat/strl.h>

#include "psx_events.h"
#include "irq.h"
#include "frontio.h"

#include "../../beetle_psx_globals.h"

/* Globals defined in libretro.cpp.  Duplicating these declarations
 * here (rather than including psx.h which is C++-only) keeps frontio.c
 * compilable as plain C. */
extern uint8_t analog_combo[2];
extern uint8_t analog_combo_hold;

#include <compat/msvc.h>
#include <retro_miscellaneous.h>
#include <streams/file_stream.h>

#include "../state_helpers.h"
#include "../mednafen.h"
#include "../../osd_message.h"
#include "../video/surface.h"

extern bool setting_apply_analog_default;

/* Factory functions for the input device implementations that
 * follow at the bottom of this file.  Each one returns a freshly-
 * allocated InputDevice subclass on the heap; the FrontIO owns
 * the pointer and deletes it in MapDevicesToPorts / ~FrontIO. */
static InputDevice *Device_Gamepad_Create(void);
static InputDevice *Device_DualAnalog_Create(bool joystick_mode);
static InputDevice *Device_DualShock_Create(const char *name);
static InputDevice *Device_Mouse_Create(void);
static InputDevice *Device_neGcon_Create(void);
static InputDevice *Device_neGconRumble_Create(const char *name);
static InputDevice *Device_GunCon_Create(void);
static InputDevice *Device_Justifier_Create(void);
static InputDevice *Device_Memcard_Create(void);
static void        Device_Memcard_Power(InputDevice *memcard);
static void        Device_Memcard_Format(InputDevice *memcard);


/* Forward declarations - all static methods, vtables, and factory functions defined below get declared here so their order in the file does not matter. */
InputDevice *Device_Gamepad_Create(void);
InputDevice *Device_DualAnalog_Create(bool joystick_mode);
InputDevice *Device_DualShock_Create(const char *name);
InputDevice *Device_Mouse_Create(void);
InputDevice *Device_neGcon_Create(void);
InputDevice *Device_neGconRumble_Create(const char *name);
InputDevice *Device_GunCon_Create(void);
InputDevice *Device_Justifier_Create(void);
InputDevice *Device_Memcard_Create(void);
InputDevice *Device_Multitap_Create(void);
static InputDevice *InputDevice_New(void);
static void InputDevice_Power(InputDevice *self_);
static int InputDevice_StateAction(InputDevice *self_, StateMem* sm, int load, int data_only, const char* section_name);
static void InputDevice_Update(InputDevice *self_, const int32_t timestamp);
static void InputDevice_ResetTS(InputDevice *self_);
static void InputDevice_SetAMCT(InputDevice *self_, bool);
static void InputDevice_SetCrosshairsCursor(InputDevice *self_, int cursor);
static void InputDevice_SetCrosshairsColor(InputDevice *self_, uint32_t color);
INLINE void InputDevice_DrawCrosshairs(InputDevice *self_, uint32_t *pixels, const unsigned width, const unsigned pix_clock, const unsigned surf_pitchinpix, const unsigned upscale_factor);
int FrontIO_StateAction(FrontIO *self_, StateMem* sm, int load, int data_only);
static int32_t InputDevice_GPULineHook(InputDevice *self_, const int32_t timestamp, bool vsync, uint32_t *pixels, const unsigned width, const unsigned pix_clock_offset, const unsigned pix_clock, const unsigned pix_clock_divider, const unsigned surf_pitchinpix, const unsigned upscale_factor);
static void InputDevice_UpdateInput(InputDevice *self_, const void *data);
static void InputDevice_SetDTR(InputDevice *self_, bool new_dtr);
static bool InputDevice_GetDSR(InputDevice *self_);
static bool InputDevice_Clock(InputDevice *self_, bool TxD, int32_t *dsr_pulse_delay);
static uint8_t * InputDevice_GetNVData(InputDevice *self_);
static uint32_t InputDevice_GetNVSize(InputDevice *self_);
static void InputDevice_ReadNV(InputDevice *self_, uint8_t *buffer, uint32_t offset, uint32_t count);
static void InputDevice_WriteNV(InputDevice *self_, const uint8_t *buffer, uint32_t offset, uint32_t count);
static uint64_t InputDevice_GetNVDirtyCount(InputDevice *self_);
static void InputDevice_ResetNVDirtyCount(InputDevice *self_);
InputDevice * FrontIO_GetMemcardDevice(FrontIO *self_, unsigned int which);
static void FrontIO_MapDevicesToPorts(FrontIO *self_);
static void FrontIO_Ctor(FrontIO *self_, bool emulate_memcards_[8], bool emulate_multitap_[2]);
void FrontIO_SetAMCT(FrontIO *self_, bool enabled);
void FrontIO_SetCrosshairsCursor(FrontIO *self_, unsigned port, int cursor);
void FrontIO_SetCrosshairsColor(FrontIO *self_, unsigned port, uint32_t color);
static void FrontIO_Destroy(FrontIO *self_);
int32_t FrontIO_CalcNextEventTS(FrontIO *self_, int32_t timestamp, int32_t next_event);
static void FrontIO_CheckStartStopPending(FrontIO *self_, int32_t timestamp, bool skip_event_set);
static INLINE void FrontIO_DoDSRIRQ(FrontIO *self_);
void FrontIO_Write(FrontIO *self_, int32_t timestamp, uint32_t A, uint32_t V);
uint32_t FrontIO_Read(FrontIO *self_, int32_t timestamp, uint32_t A);
int32_t FrontIO_Update(FrontIO *self_, int32_t timestamp);
void FrontIO_ResetTS(FrontIO *self_);
void FrontIO_Power(FrontIO *self_);
void FrontIO_UpdateInput(FrontIO *self_);
void FrontIO_SetInput(FrontIO *self_, unsigned int port, const char *type, void *ptr);
uint64_t FrontIO_GetMemcardDirtyCount(FrontIO *self_, unsigned int which);
void FrontIO_LoadMemcard(FrontIO *self_, unsigned int which);
void FrontIO_LoadMemcardFromPath(FrontIO *self_, unsigned int which, const char *path, bool force_load);
void FrontIO_SaveMemcard(FrontIO *self_, unsigned int which);
void FrontIO_SaveMemcardToPath(FrontIO *self_, unsigned int which, const char *path, bool force_save);
void FrontIO_GPULineHook(FrontIO *self_, const int32_t timestamp, const int32_t line_timestamp, bool vsync, uint32_t *pixels, const unsigned width, const unsigned pix_clock_offset, const unsigned pix_clock, const unsigned pix_clock_divider, const unsigned surf_pitchinpix, const unsigned upscale_factor);
static void InputDevice_Gamepad_Ctor(InputDevice *self_);
static void InputDevice_Gamepad_Power(InputDevice *self_);
static int InputDevice_Gamepad_StateAction(InputDevice *self_, StateMem* sm, int load, int data_only, const char* section_name);
static void InputDevice_Gamepad_UpdateInput(InputDevice *self_, const void *data);
static void InputDevice_Gamepad_SetDTR(InputDevice *self_, bool new_dtr);
static bool InputDevice_Gamepad_GetDSR(InputDevice *self_);
static bool InputDevice_Gamepad_Clock(InputDevice *self_, bool TxD, int32_t *dsr_pulse_delay);
static void InputDevice_DualAnalog_Ctor(InputDevice *self_, bool joystick_mode_);
static void InputDevice_DualAnalog_Power(InputDevice *self_);
static int InputDevice_DualAnalog_StateAction(InputDevice *self_, StateMem* sm, int load, int data_only, const char* section_name);
static void InputDevice_DualAnalog_UpdateInput(InputDevice *self_, const void *data);
static void InputDevice_DualAnalog_SetDTR(InputDevice *self_, bool new_dtr);
static bool InputDevice_DualAnalog_GetDSR(InputDevice *self_);
static bool InputDevice_DualAnalog_Clock(InputDevice *self_, bool TxD, int32_t *dsr_pulse_delay);
static void InputDevice_DualShock_Ctor(InputDevice *self_, const char *name);
static void InputDevice_DualShock_Update(InputDevice *self_, const int32_t timestamp);
static void InputDevice_DualShock_ResetTS(InputDevice *self_);
static void InputDevice_DualShock_SetAMCT(InputDevice *self_, bool enabled);
static void InputDevice_DualShock_CheckManualAnaModeChange(InputDevice *self_);
static void InputDevice_DualShock_Power(InputDevice *self_);
static int InputDevice_DualShock_StateAction(InputDevice *self_, StateMem* sm, int load, int data_only, const char* section_name);
static void InputDevice_DualShock_UpdateInput(InputDevice *self_, const void *data);
static void InputDevice_DualShock_SetDTR(InputDevice *self_, bool new_dtr);
static bool InputDevice_DualShock_GetDSR(InputDevice *self_);
static bool InputDevice_DualShock_Clock(InputDevice *self_, bool TxD, int32_t *dsr_pulse_delay);
static void InputDevice_Mouse_Ctor(InputDevice *self_);
static void InputDevice_Mouse_Update(InputDevice *self_, const int32_t timestamp);
static void InputDevice_Mouse_ResetTS(InputDevice *self_);
static void InputDevice_Mouse_Power(InputDevice *self_);
static int InputDevice_Mouse_StateAction(InputDevice *self_, StateMem* sm, int load, int data_only, const char* section_name);
static void InputDevice_Mouse_UpdateInput(InputDevice *self_, const void *data);
static void InputDevice_Mouse_SetDTR(InputDevice *self_, bool new_dtr);
static bool InputDevice_Mouse_Clock(InputDevice *self_, bool TxD, int32_t *dsr_pulse_delay);
static void InputDevice_neGcon_Ctor(InputDevice *self_);
static void InputDevice_neGcon_Power(InputDevice *self_);
static void InputDevice_neGcon_UpdateInput(InputDevice *self_, const void *data);
static void InputDevice_neGcon_SetDTR(InputDevice *self_, bool new_dtr);
static bool InputDevice_neGcon_GetDSR(InputDevice *self_);
static bool InputDevice_neGcon_Clock(InputDevice *self_, bool TxD, int32_t *dsr_pulse_delay);
static void InputDevice_neGconRumble_Ctor(InputDevice *self_, const char *name);
static void InputDevice_neGconRumble_Update(InputDevice *self_, const int32_t timestamp);
static void InputDevice_neGconRumble_ResetTS(InputDevice *self_);
static void InputDevice_neGconRumble_SetAMCT(InputDevice *self_, bool enabled);
static void InputDevice_neGconRumble_CheckManualAnaModeChange(InputDevice *self_);
static void InputDevice_neGconRumble_Power(InputDevice *self_);
static int InputDevice_neGconRumble_StateAction(InputDevice *self_, StateMem* sm, int load, int data_only, const char* section_name);
static void InputDevice_neGconRumble_UpdateInput(InputDevice *self_, const void *data);
static void InputDevice_neGconRumble_SetDTR(InputDevice *self_, bool new_dtr);
static bool InputDevice_neGconRumble_GetDSR(InputDevice *self_);
static bool InputDevice_neGconRumble_Clock(InputDevice *self_, bool TxD, int32_t *dsr_pulse_delay);
static void InputDevice_GunCon_Ctor(InputDevice *self_);
static void InputDevice_GunCon_Power(InputDevice *self_);
static int InputDevice_GunCon_StateAction(InputDevice *self_, StateMem* sm, int load, int data_only, const char* section_name);
static void InputDevice_GunCon_UpdateInput(InputDevice *self_, const void *data);
static int32_t InputDevice_GunCon_GPULineHook(InputDevice *self_, const int32_t line_timestamp, bool vsync, uint32_t *pixels, const unsigned width,
      const unsigned pix_clock_offset, const unsigned pix_clock, const unsigned pix_clock_divider, const unsigned surf_pitchinpix, const unsigned upscale_factor);
static void InputDevice_GunCon_SetDTR(InputDevice *self_, bool new_dtr);
static bool InputDevice_GunCon_GetDSR(InputDevice *self_);
static bool InputDevice_GunCon_Clock(InputDevice *self_, bool TxD, int32_t *dsr_pulse_delay);
static void InputDevice_Justifier_Ctor(InputDevice *self_);
static void InputDevice_Justifier_Power(InputDevice *self_);
static void InputDevice_Justifier_UpdateInput(InputDevice *self_, const void *data);
static int InputDevice_Justifier_StateAction(InputDevice *self_, StateMem* sm, int load, int data_only, const char* section_name);
static int32_t InputDevice_Justifier_GPULineHook(InputDevice *self_, const int32_t timestamp, bool vsync, uint32_t *pixels, const unsigned width, const unsigned pix_clock_offset, const unsigned pix_clock, const unsigned pix_clock_divider, const unsigned surf_pitchinpix, const unsigned upscale_factor);
static void InputDevice_Justifier_SetDTR(InputDevice *self_, bool new_dtr);
static bool InputDevice_Justifier_GetDSR(InputDevice *self_);
static bool InputDevice_Justifier_Clock(InputDevice *self_, bool TxD, int32_t *dsr_pulse_delay);
static void InputDevice_Memcard_Format(InputDevice *self_);
static void InputDevice_Memcard_Ctor(InputDevice *self_);
static void InputDevice_Memcard_Power(InputDevice *self_);
static int InputDevice_Memcard_StateAction(InputDevice *self_, StateMem* sm, int load, int data_only, const char* section_name);
static void InputDevice_Memcard_SetDTR(InputDevice *self_, bool new_dtr);
static bool InputDevice_Memcard_GetDSR(InputDevice *self_);
static bool InputDevice_Memcard_Clock(InputDevice *self_, bool TxD, int32_t *dsr_pulse_delay);
static uint8_t * InputDevice_Memcard_GetNVData(InputDevice *self_);
static uint32_t InputDevice_Memcard_GetNVSize(InputDevice *self_);
static void InputDevice_Memcard_ReadNV(InputDevice *self_, uint8_t *buffer, uint32_t offset, uint32_t size);
static void InputDevice_Memcard_WriteNV(InputDevice *self_, const uint8_t *buffer, uint32_t offset, uint32_t size);
static uint64_t InputDevice_Memcard_GetNVDirtyCount(InputDevice *self_);
static void InputDevice_Memcard_ResetNVDirtyCount(InputDevice *self_);
static void InputDevice_Multitap_Ctor(InputDevice *self_);
static void InputDevice_Multitap_SetSubDevice(InputDevice *self_, unsigned int sub_index, InputDevice *device, InputDevice *mc_device);
static void InputDevice_Multitap_Power(InputDevice *self_);
static int InputDevice_Multitap_StateAction(InputDevice *self_, StateMem* sm, int load, int data_only, const char* section_name);
static void InputDevice_Multitap_SetDTR(InputDevice *self_, bool new_dtr);
static bool InputDevice_Multitap_GetDSR(InputDevice *self_);
static bool InputDevice_Multitap_Clock(InputDevice *self_, bool TxD, int32_t *dsr_pulse_delay);
static InputDevice * InputDevice_New(void);

/* Multitap class definition - was in input/multitap.h.  Kept
 * file-local because the FrontIO holds it as
 * InputDevice_Multitap *DevicesTap[2] (concrete pointer type
 * needed for the non-virtual SetSubDevice method) and nothing
 * else in the codebase touches it. */
typedef struct InputDevice_Multitap InputDevice_Multitap;
struct InputDevice_Multitap
{
   InputDevice base;
         InputDevice *pad_devices[4];
         InputDevice *mc_devices[4];
         bool dtr;
         int selected_device;
         bool full_mode_setting;
         bool full_mode;
         bool mc_mode;
         bool prev_fm_success;
         uint8_t fm_dp;
         uint8_t fm_buffer[4][8];
         uint8_t sb[4][8];
         bool fm_command_error;
         uint8_t command;
         uint8_t receive_buffer;
         uint8_t bit_counter;
         uint8_t byte_counter;
};






struct FrontIO
{
         bool emulate_memcards[8];
         bool emulate_multitap[2];
         InputDevice *Ports[2];
         InputDevice *MCPorts[2];
         InputDevice *DummyDevice;
         InputDevice_Multitap *DevicesTap[2];
         InputDevice *Devices[8];
         void *DeviceData[8];
         InputDevice *DevicesMC[8];
         int32_t ClockDivider;
         bool ReceivePending;
         bool TransmitPending;
         bool ReceiveInProgress;
         bool TransmitInProgress;
         bool ReceiveBufferAvail;
         uint8_t ReceiveBuffer;
         uint8_t TransmitBuffer;
         int32_t ReceiveBitCounter;
         int32_t TransmitBitCounter;
         uint16_t Mode;
         uint16_t Control;
         uint16_t Baudrate;
         bool istatus;
         int32_t irq10_pulse_ts[2];
         int32_t dsr_pulse_delay[4];
         int32_t dsr_active_until_ts[4];
         int32_t lastts;
         bool amct_enabled;
         int chair_cursor[8];
         uint32_t chair_colors[8];
};

static void InputDevice_Power(InputDevice *self_)
{
   (void)self_;

}

static int InputDevice_StateAction(InputDevice *self_, StateMem* sm, int load, int data_only, const char* section_name)
{
   (void)self_;

 return(1);
}

static void InputDevice_Update(InputDevice *self_, const int32_t timestamp)
{
   (void)self_;


}

static void InputDevice_ResetTS(InputDevice *self_)
{
   (void)self_;


}

static void InputDevice_SetAMCT(InputDevice *self_, bool a) { }

static void InputDevice_SetCrosshairsCursor(InputDevice *self_, int cursor)
{
   InputDevice *self = self_;

   if ( cursor >= 0 && cursor < SETTING_GUN_CROSSHAIR_LAST )
      self->chair_cursor = cursor;
}

static void InputDevice_SetCrosshairsColor(InputDevice *self_, uint32_t color)
{
   InputDevice *self = self_;

   self->chair_r = (color >> 16) & 0xFF;
   self->chair_g = (color >>  8) & 0xFF;
   self->chair_b = (color >>  0) & 0xFF;
}

static void crosshair_plot( uint32_t *pixels,
							int x,
							unsigned chair_r,
							unsigned chair_g,
							unsigned chair_b )
{
	int r, g, b, a;
	int nr, ng, nb;

	MDFN_DecodeColor(pixels[x], &r, &g, &b, &a);

	nr = (r + chair_r * 3) >> 2;
	ng = (g + chair_g * 3) >> 2;
	nb = (b + chair_b * 3) >> 2;

	if ( (int)((abs(r - nr) - 0x40) & (abs(g - ng) - 0x40) & (abs(b - nb) - 0x40)) < 0)
	{
		if((nr | ng | nb) & 0x80)
		{
			nr >>= 1;
			ng >>= 1;
			nb >>= 1;
		}
		else
		{
			nr ^= 0x80;
			ng ^= 0x80;
			nb ^= 0x80;
		}
	}

	pixels[x] = MAKECOLOR(nr, ng, nb, a);
}

INLINE void InputDevice_DrawCrosshairs(InputDevice *self_, uint32_t *pixels, const unsigned width, const unsigned pix_clock, const unsigned surf_pitchinpix, const unsigned upscale_factor)
{
   int row;
   int32_t x;
   InputDevice *self = self_;

	switch ( self->chair_cursor )
	{

	case SETTING_GUN_CROSSHAIR_OFF:
		return;

	case SETTING_GUN_CROSSHAIR_CROSS:

		if ( self->chair_y >= -8 && self->chair_y <= 8 )
		{
			int32_t ic;
			int32_t x_start, x_bound;

			if ( self->chair_y == 0 ) {
				ic = pix_clock / 762925;
			} else {
				ic = 0;
			}

			x_start = (self->chair_x - ic) * upscale_factor;
			if (x_start < 0) x_start = 0;
			x_bound = (self->chair_x + ic + 1) * upscale_factor;
			{
				int32_t _max = width * upscale_factor;
				if (x_bound > _max) x_bound = _max;
			}

			for (x = x_start; x < x_bound; x++ )
			{
            for (row = 0; row < upscale_factor; row++)
            {
               crosshair_plot( pixels, x + (row * surf_pitchinpix), self->chair_r, self->chair_g, self->chair_b );
            }
			}
		}

		break;

	case SETTING_GUN_CROSSHAIR_DOT:

		if ( self->chair_y >= -1 && self->chair_y <= 1 )
		{
			int32_t ic;
			int32_t x_start, x_bound;

			ic = pix_clock / ( 762925 * 6 );

			x_start = (self->chair_x - ic) * upscale_factor;
			if (x_start < 0) x_start = 0;
			x_bound = (self->chair_x + ic) * upscale_factor;
			{
				int32_t _max = width * upscale_factor;
				if (x_bound > _max) x_bound = _max;
			}

			for (x = x_start; x < x_bound; x++ )
			{
            for (row = 0; row < upscale_factor; row++)
            {
               crosshair_plot( pixels, x + (row * surf_pitchinpix), self->chair_r, self->chair_g, self->chair_b );
            }
			}
		}

		break;

	}; /*  switch ( chair_cursor ) */
}

int FrontIO_StateAction(FrontIO *self_, StateMem* sm, int load, int data_only)
{
   unsigned i;
   FrontIO *self = self_;

 SFORMAT StateRegs[] =
 {
  SFVAR(self->ClockDivider),

  SFVAR(self->ReceivePending),
  SFVAR(self->TransmitPending),

  SFVAR(self->ReceiveInProgress),
  SFVAR(self->TransmitInProgress),

  SFVAR(self->ReceiveBufferAvail),

  SFVAR(self->ReceiveBuffer),
  SFVAR(self->TransmitBuffer),

  SFVAR(self->ReceiveBitCounter),
  SFVAR(self->TransmitBitCounter),

  SFVAR(self->Mode),
  SFVAR(self->Control),
  SFVAR(self->Baudrate),

  SFVAR(self->istatus),

  /*  FIXME: Step mode save states. */
  SFARRAY32(self->irq10_pulse_ts, sizeof(self->irq10_pulse_ts) / sizeof(self->irq10_pulse_ts[0])),
  SFARRAY32(self->dsr_pulse_delay, sizeof(self->dsr_pulse_delay) / sizeof(self->dsr_pulse_delay[0])),
  SFARRAY32(self->dsr_active_until_ts, sizeof(self->dsr_active_until_ts) / sizeof(self->dsr_active_until_ts[0])),

  SFEND
 };

 int ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, "FIO");

 for (i = 0; i < 8; i++)
 {
  char tmpbuf[32];
  snprintf(tmpbuf, sizeof(tmpbuf), "FIODEV%u", i);

  ret &= (self->Devices[i])->vt->StateAction((self->Devices[i]), sm, load, data_only, tmpbuf);
 }

 for (i = 0; i < 8; i++)
 {
  char tmpbuf[32];
  snprintf(tmpbuf, sizeof(tmpbuf), "FIOMC%u", i);

  ret &= (self->DevicesMC[i])->vt->StateAction((self->DevicesMC[i]), sm, load, data_only, tmpbuf);
 }

 for (i = 0; i < 2; i++)
 {
  char tmpbuf[32];
  snprintf(tmpbuf, sizeof(tmpbuf), "FIOTAP%u", i);

  ret &= (&self->DevicesTap[i]->base)->vt->StateAction((&self->DevicesTap[i]->base), sm, load, data_only, tmpbuf);
 }

 if(load)
 {
    IRQ_Assert(IRQ_SIO, self->istatus);
 }

 return(ret);
}

static int32_t InputDevice_GPULineHook(InputDevice *self_, const int32_t timestamp, bool vsync, uint32_t *pixels, const unsigned width, const unsigned pix_clock_offset, const unsigned pix_clock, const unsigned pix_clock_divider, const unsigned surf_pitchinpix, const unsigned upscale_factor)
{
   (void)self_;

 return(PSX_EVENT_MAXTS);
}


static void InputDevice_UpdateInput(InputDevice *self_, const void *data)
{
   (void)self_;

}


static void InputDevice_SetDTR(InputDevice *self_, bool new_dtr)
{
   (void)self_;


}

static bool InputDevice_GetDSR(InputDevice *self_)
{
   (void)self_;

   return 0;
}

static bool InputDevice_Clock(InputDevice *self_, bool TxD, int32_t *dsr_pulse_delay)
{
   (void)self_;

   *dsr_pulse_delay = 0;

   return 1;
}


static uint8_t *InputDevice_GetNVData(InputDevice *self_)
{
   (void)self_;

   return NULL;
}
static uint32_t InputDevice_GetNVSize(InputDevice *self_)
{
   (void)self_;

   return 0;
}

static void InputDevice_ReadNV(InputDevice *self_, uint8_t *buffer, uint32_t offset, uint32_t count)
{
   (void)self_;


}

static void InputDevice_WriteNV(InputDevice *self_, const uint8_t *buffer, uint32_t offset, uint32_t count)
{
   (void)self_;


}

static uint64_t InputDevice_GetNVDirtyCount(InputDevice *self_)
{
   (void)self_;

   return 0;
}

static void InputDevice_ResetNVDirtyCount(InputDevice *self_)
{
   (void)self_;


}

static unsigned EP_to_MP(bool emulate_multitap[2], unsigned ep)
{
   if(!emulate_multitap[0] && emulate_multitap[1])
   {
      if(ep == 0 || ep >= 5)
         return 0;
      return 1;
   }
   return(ep >= 4);
}

static INLINE unsigned EP_to_SP(bool emulate_multitap[2], unsigned ep)
{
   if(!emulate_multitap[0] && emulate_multitap[1])
   {
      if(ep == 0)
         return(0);
      else if(ep < 5)
         return(ep - 1);
      return(ep - 4);
   }
   return(ep & 0x3);
}

InputDevice * FrontIO_GetMemcardDevice(FrontIO *self_, unsigned int which)
{
   FrontIO *self = self_;

   if (self->DevicesMC[which])
      return self->DevicesMC[which];
   return NULL;
}

static void FrontIO_MapDevicesToPorts(FrontIO *self_)
{
   FrontIO *self = self_;

   int i;
   if(self->emulate_multitap[0] && self->emulate_multitap[1])
   {
      for (i = 0; i < 2; i++)
      {
         self->Ports[i] = &self->DevicesTap[i]->base;
         self->MCPorts[i] = self->DummyDevice;
      }
   }
   else if(!self->emulate_multitap[0] && self->emulate_multitap[1])
   {
      self->Ports[0] = self->Devices[0];
      self->MCPorts[0] = self->emulate_memcards[0] ? self->DevicesMC[0] : self->DummyDevice;

      self->Ports[1] = &self->DevicesTap[1]->base;
      self->MCPorts[1] = self->DummyDevice;
   }
   else if(self->emulate_multitap[0] && !self->emulate_multitap[1])
   {
      self->Ports[0] = &self->DevicesTap[0]->base;
      self->MCPorts[0] = self->DummyDevice;

      self->Ports[1] = self->Devices[4];
      self->MCPorts[1] = self->emulate_memcards[4] ? self->DevicesMC[4] : self->DummyDevice;
   }
   else
   {
      for(i = 0; i < 2; i++)
      {
         self->Ports[i] = self->Devices[i];
         self->MCPorts[i] = self->emulate_memcards[i] ? self->DevicesMC[i] : self->DummyDevice;
      }
   }

   /* printf("\n"); */
   for(i = 0; i < 8; i++)
   {
      unsigned mp = EP_to_MP(self->emulate_multitap, i);

      if(self->emulate_multitap[mp])
         InputDevice_Multitap_SetSubDevice(&self->DevicesTap[mp]->base, EP_to_SP(self->emulate_multitap, i), self->Devices[i], self->emulate_memcards[i] ? self->DevicesMC[i] : self->DummyDevice);
      else
         InputDevice_Multitap_SetSubDevice(&self->DevicesTap[mp]->base, EP_to_SP(self->emulate_multitap, i), self->DummyDevice, self->DummyDevice);

      /* printf("%d-> multitap: %d, sub-port: %d\n", i, mp, EP_to_SP(emulate_multitap, i)); */
   }
}

static void FrontIO_Ctor(FrontIO *self_, bool emulate_memcards_[8], bool emulate_multitap_[2])
{
   FrontIO *self = self_;

   int i;

   /* Zero-initialize all pointer members up front so the destructor
    * runs cleanly if any allocation below fails. The destructor
    * already null-guards each delete; we just need the slots that
    * never got assigned to read as NULL rather than garbage. */
   self->DummyDevice = NULL;
   for (i = 0; i < 8; i++)
   {
      self->Devices[i]    = NULL;
      self->DevicesMC[i]  = NULL;
      self->DeviceData[i] = NULL;
   }
   for (i = 0; i < 2; i++)
      self->DevicesTap[i] = NULL;

   memcpy(self->emulate_memcards, emulate_memcards_, sizeof(self->emulate_memcards));
   memcpy(self->emulate_multitap, emulate_multitap_, sizeof(self->emulate_multitap));

   self->DummyDevice = InputDevice_New();

   for (i = 0; i < 8; i++)
   {
      self->Devices[i]      = InputDevice_New();
      self->DevicesMC[i]    = Device_Memcard_Create();
      self->chair_cursor[i] = SETTING_GUN_CROSSHAIR_CROSS;
      (self->Devices[i])->vt->SetCrosshairsCursor((self->Devices[i]), self->chair_cursor[i]);
      self->chair_colors[i] = 1 << 24;
      (self->Devices[i])->vt->SetCrosshairsColor((self->Devices[i]), self->chair_colors[i]);
   }

   for (i = 0; i < 2; i++)
      self->DevicesTap[i] = (InputDevice_Multitap *)Device_Multitap_Create();

   FrontIO_MapDevicesToPorts(self_);
}

void FrontIO_SetAMCT(FrontIO *self_, bool enabled)
{
   FrontIO *self = self_;

   int i;
   for(i = 0; i < 8; i++)
      (self->Devices[i])->vt->SetAMCT((self->Devices[i]), enabled);
   self->amct_enabled = enabled;
}

void FrontIO_SetCrosshairsCursor(FrontIO *self_, unsigned port, int cursor)
{
   FrontIO *self = self_;

   self->chair_cursor[port] = cursor;
   (self->Devices[port])->vt->SetCrosshairsCursor((self->Devices[port]), cursor);
}

void FrontIO_SetCrosshairsColor(FrontIO *self_, unsigned port, uint32_t color)
{
   FrontIO *self = self_;

   self->chair_colors[port] = color;
   (self->Devices[port])->vt->SetCrosshairsColor((self->Devices[port]), color);
}

static void FrontIO_Destroy(FrontIO *self_)
{
   FrontIO *self = self_;

   int i;
   for(i = 0; i < 8; i++)
   {
      if(self->Devices[i])
         free(self->Devices[i]);
      self->Devices[i] = NULL;
      if(self->DevicesMC[i])
         free(self->DevicesMC[i]);
      self->DevicesMC[i] = NULL;
   }

   for(i = 0; i < 2; i++)
   {
      if(self->DevicesTap[i])
         free(self->DevicesTap[i]);
      self->DevicesTap[i] = NULL;
   }

   if(self->DummyDevice)
      free(self->DummyDevice);
   self->DummyDevice = NULL;
}

int32_t FrontIO_CalcNextEventTS(FrontIO *self_, int32_t timestamp, int32_t next_event)
{
   FrontIO *self = self_;

   int32_t ret;
   int i;

   if(self->ClockDivider > 0 && self->ClockDivider < next_event)
      next_event = self->ClockDivider;

   for(i = 0; i < 4; i++)
      if(self->dsr_pulse_delay[i] > 0 && next_event > self->dsr_pulse_delay[i])
         next_event = self->dsr_pulse_delay[i];

   overclock_device_to_cpu(&next_event);

   ret = timestamp + next_event;

   /* XXX Not sure if this is overclock-proof. This is probably only
      useful for lightgun support however */
   if(self->irq10_pulse_ts[0] < ret)
      ret = self->irq10_pulse_ts[0];

   if(self->irq10_pulse_ts[1] < ret)
      ret = self->irq10_pulse_ts[1];

   return(ret);
}

static const uint8_t ScaleShift[4] = { 0, 0, 4, 6 };

static void FrontIO_CheckStartStopPending(FrontIO *self_, int32_t timestamp, bool skip_event_set)
{
   FrontIO *self = self_;

   /* const bool prior_ReceiveInProgress = ReceiveInProgress; */
   /* const bool prior_TransmitInProgress = TransmitInProgress; */
   bool trigger_condition = false;

   trigger_condition = (self->ReceivePending && (self->Control & 0x4)) || (self->TransmitPending && (self->Control & 0x1));

   if(trigger_condition)
   {
      if(self->ReceivePending)
      {
         self->ReceivePending = false;
         self->ReceiveInProgress = true;
         self->ReceiveBufferAvail = false;
         self->ReceiveBuffer = 0;
         self->ReceiveBitCounter = 0;
      }

      if(self->TransmitPending)
      {
         self->TransmitPending = false;
         self->TransmitInProgress = true;
         self->TransmitBitCounter = 0;
      }

      self->ClockDivider = MAX(0x20, (self->Baudrate << ScaleShift[self->Mode & 0x3]) & ~1); /*  Minimum of 0x20 is an emulation sanity check to prevent severe performance degradation. */
      /* printf("CD: 0x%02x\n", ClockDivider); */
   }

   if(!(self->Control & 0x5))
   {
      self->ReceiveInProgress = false;
      self->TransmitInProgress = false;
   }

   if(!self->ReceiveInProgress && !self->TransmitInProgress)
      self->ClockDivider = 0;

   if(!(skip_event_set))
      PSX_SetEventNT(PSX_EVENT_FIO, FrontIO_CalcNextEventTS(self_, timestamp, 0x10000000));
}

/*  DSR IRQ bit setting appears(from indirect tests on real PS1) to be level-sensitive, not edge-sensitive */
static INLINE void FrontIO_DoDSRIRQ(FrontIO *self_)
{
   FrontIO *self = self_;

   if(self->Control & 0x1000)
   {
      self->istatus = true;
      IRQ_Assert(IRQ_SIO, true);
   }
}


void FrontIO_Write(FrontIO *self_, int32_t timestamp, uint32_t A, uint32_t V)
{
   FrontIO *self = self_;

   assert(!(A & 0x1));

   FrontIO_Update(self_, timestamp);

   switch(A & 0xF)
   {
      case 0x0:
         self->TransmitBuffer = V;
         self->TransmitPending = true;
         self->TransmitInProgress = false;
         break;

      case 0x8:
         self->Mode = V & 0x013F;
         break;

      case 0xa:
         self->Control = V & 0x3F2F;

         if(V & 0x10)
         {
            self->istatus = false;
            IRQ_Assert(IRQ_SIO, false);
         }

         if(V & 0x40)	/*  Reset */
         {
            self->istatus = false;
            IRQ_Assert(IRQ_SIO, false);

            self->ClockDivider = 0;
            self->ReceivePending = false;
            self->TransmitPending = false;

            self->ReceiveInProgress = false;
            self->TransmitInProgress = false;

            self->ReceiveBufferAvail = false;

            self->TransmitBuffer = 0;
            self->ReceiveBuffer = 0;

            self->ReceiveBitCounter = 0;
            self->TransmitBitCounter = 0;

            self->Mode = 0;
            self->Control = 0;
            self->Baudrate = 0;
         }

         (self->Ports[0])->vt->SetDTR((self->Ports[0]), (self->Control & 0x2) && !(self->Control & 0x2000));
         (self->MCPorts[0])->vt->SetDTR((self->MCPorts[0]), (self->Control & 0x2) && !(self->Control & 0x2000));
         (self->Ports[1])->vt->SetDTR((self->Ports[1]), (self->Control & 0x2) && (self->Control & 0x2000));
         (self->MCPorts[1])->vt->SetDTR((self->MCPorts[1]), (self->Control & 0x2) && (self->Control & 0x2000));

#if 1
         if(!((self->Control & 0x2) && !(self->Control & 0x2000)))
         {
            self->dsr_pulse_delay[0] = 0;
            self->dsr_pulse_delay[2] = 0;
            self->dsr_active_until_ts[0] = -1;
            self->dsr_active_until_ts[2] = -1;
         }

         if(!((self->Control & 0x2) && (self->Control & 0x2000)))
         {
            self->dsr_pulse_delay[1] = 0;
            self->dsr_pulse_delay[3] = 0;
            self->dsr_active_until_ts[1] = -1;
            self->dsr_active_until_ts[3] = -1;
         }

#endif
         /*  TODO: Uncomment out in the future once our CPU emulation is a bit more accurate with timing, to prevent causing problems with games */
         /*  that may clear the IRQ in an unsafe pattern that only works because its execution was slow enough to allow DSR to go inactive.  (Whether or not */
         /*  such games even exist though is unknown!) */
         /* if(timestamp < dsr_active_until_ts[0] || timestamp < dsr_active_until_ts[1] || timestamp < dsr_active_until_ts[2] || timestamp < dsr_active_until_ts[3]) */
         /*  FrontIO_DoDSRIRQ(self_); */

         break;

      case 0xe:
         self->Baudrate = V;
         /* printf("%02x\n", V); */
         break;
   }

   FrontIO_CheckStartStopPending(self_, timestamp, false);
}


uint32_t FrontIO_Read(FrontIO *self_, int32_t timestamp, uint32_t A)
{
   FrontIO *self = self_;

   uint32_t ret = 0;

   assert(!(A & 0x1));

   FrontIO_Update(self_, timestamp);

   switch(A & 0xF)
   {
      case 0x0:
         /* printf("FIO Read: 0x%02x\n", ReceiveBuffer); */
         ret = self->ReceiveBuffer | (self->ReceiveBuffer << 8) | (self->ReceiveBuffer << 16) | (self->ReceiveBuffer << 24);
         self->ReceiveBufferAvail = false;
         self->ReceivePending = true;
         self->ReceiveInProgress = false;
         FrontIO_CheckStartStopPending(self_, timestamp, false);
         break;

      case 0x4:
         ret = 0;

         if(!self->TransmitPending && !self->TransmitInProgress)
            ret |= 0x1;

         if(self->ReceiveBufferAvail)
            ret |= 0x2;

         if(timestamp < self->dsr_active_until_ts[0] || timestamp < self->dsr_active_until_ts[1] || timestamp < self->dsr_active_until_ts[2] || timestamp < self->dsr_active_until_ts[3])
            ret |= 0x80;

         if(self->istatus)
            ret |= 0x200;

         break;

      case 0x8:
         ret = self->Mode;
         break;

      case 0xa:
         ret = self->Control;
         break;

      case 0xe:
         ret = self->Baudrate;
         break;
   }

   return(ret);
}

int32_t FrontIO_Update(FrontIO *self_, int32_t timestamp)
{
   FrontIO *self = self_;

   int32_t clocks, i;
   bool need_start_stop_check = false;

   clocks = timestamp - self->lastts;

   overclock_cpu_to_device(&clocks);

   for(i = 0; i < 4; i++)
      if(self->dsr_pulse_delay[i] > 0)
      {
         self->dsr_pulse_delay[i] -= clocks;
         if(self->dsr_pulse_delay[i] <= 0)
         {
            int32_t off = 32 + self->dsr_pulse_delay[i];

            overclock_device_to_cpu(&off);

            self->dsr_active_until_ts[i] = timestamp + off;
            FrontIO_DoDSRIRQ(self_);
         }
      }

   for(i = 0; i < 2; i++)
   {
      if(timestamp >= self->irq10_pulse_ts[i])
      {
         /* printf("Yay: %d %u\n", i, timestamp); */
         self->irq10_pulse_ts[i] = PSX_EVENT_MAXTS;
         IRQ_Assert(IRQ_PIO, true);
         IRQ_Assert(IRQ_PIO, false);
      }
   }

   if(self->ClockDivider > 0)
   {
      self->ClockDivider -= clocks;

      while(self->ClockDivider <= 0)
      {
         if(self->ReceiveInProgress || self->TransmitInProgress)
         {
            bool rxd = 0, txd = 0;
            const uint32_t BCMask = 0x07;

            if(self->TransmitInProgress)
            {
               txd = (self->TransmitBuffer >> self->TransmitBitCounter) & 1;
               self->TransmitBitCounter = (self->TransmitBitCounter + 1) & BCMask;
               if(!self->TransmitBitCounter)
               {
                  need_start_stop_check = true;
                  self->TransmitInProgress = false;

                  if(self->Control & 0x400)
                  {
                     self->istatus = true;
                     IRQ_Assert(IRQ_SIO, true);
                  }
               }
            }

            /* Clock all four ports unconditionally - each Clock() may
             * mutate dsr_pulse_delay[i] via its int32_t* arg, so we
             * cannot let && short-circuit. Bind each to a local first,
             * then AND. */
            {
               bool rxd_p0  = (self->Ports[0])->vt->Clock((self->Ports[0]),     txd, &self->dsr_pulse_delay[0]);
               bool rxd_p1  = (self->Ports[1])->vt->Clock((self->Ports[1]),     txd, &self->dsr_pulse_delay[1]);
               bool rxd_mc0 = (self->MCPorts[0])->vt->Clock((self->MCPorts[0]), txd, &self->dsr_pulse_delay[2]);
               bool rxd_mc1 = (self->MCPorts[1])->vt->Clock((self->MCPorts[1]), txd, &self->dsr_pulse_delay[3]);
               rxd = rxd_p0 && rxd_p1 && rxd_mc0 && rxd_mc1;
            }

            if(self->ReceiveInProgress)
            {
               self->ReceiveBuffer &= ~(1 << self->ReceiveBitCounter);
               self->ReceiveBuffer |= rxd << self->ReceiveBitCounter;

               self->ReceiveBitCounter = (self->ReceiveBitCounter + 1) & BCMask;

               if(!self->ReceiveBitCounter)
               {
                  need_start_stop_check = true;
                  self->ReceiveInProgress = false;
                  self->ReceiveBufferAvail = true;

                  if(self->Control & 0x800)
                  {
                     self->istatus = true;
                     IRQ_Assert(IRQ_SIO, true);
                  }
               }
            }
            self->ClockDivider += MAX(0x20, (self->Baudrate << ScaleShift[self->Mode & 0x3]) & ~1); /*  Minimum of 0x20 is an emulation sanity check to prevent severe performance degradation. */
         }
         else
            break;
      }
   }


   self->lastts = timestamp;


   if(need_start_stop_check)
   {
      FrontIO_CheckStartStopPending(self_, timestamp, true);
   }

   return(FrontIO_CalcNextEventTS(self_, timestamp, 0x10000000));
}

void FrontIO_ResetTS(FrontIO *self_)
{
   FrontIO *self = self_;

   int i;
   for(i = 0; i < 8; i++)
   {
      (self->Devices[i])->vt->Update((self->Devices[i]), self->lastts);	/*  Maybe eventually call FrontIO_Update(self_) from FrontIO_Update(self_) and remove this(but would hurt speed)? */
      (self->Devices[i])->vt->ResetTS((self->Devices[i]));

      (self->DevicesMC[i])->vt->Update((self->DevicesMC[i]), self->lastts);	/*  Maybe eventually call FrontIO_Update(self_) from FrontIO_Update(self_) and remove this(but would hurt speed)? */
      (self->DevicesMC[i])->vt->ResetTS((self->DevicesMC[i]));
   }

   for(i = 0; i < 2; i++)
   {
      (&self->DevicesTap[i]->base)->vt->Update((&self->DevicesTap[i]->base), self->lastts);
      (&self->DevicesTap[i]->base)->vt->ResetTS((&self->DevicesTap[i]->base));
   }

   for(i = 0; i < 2; i++)
   {
      if(self->irq10_pulse_ts[i] != PSX_EVENT_MAXTS)
         self->irq10_pulse_ts[i] -= self->lastts;
   }

   for(i = 0; i < 4; i++)
   {
      if(self->dsr_active_until_ts[i] >= 0)
      {
         self->dsr_active_until_ts[i] -= self->lastts;
         /* printf("SPOOONY: %d %d\n", i, dsr_active_until_ts[i]); */
      }
   }
   self->lastts = 0;
}


void FrontIO_Power(FrontIO *self_)
{
   FrontIO *self = self_;

   int i;
   for(i = 0; i < 4; i++)
   {
      self->dsr_pulse_delay[i] = 0;
      self->dsr_active_until_ts[i] = -1;
   }

   for(i = 0; i < 2; i++)
      self->irq10_pulse_ts[i] = PSX_EVENT_MAXTS;

   self->lastts = 0;

   /*  */
   /*  */

   self->ClockDivider = 0;

   self->ReceivePending = false;
   self->TransmitPending = false;

   self->ReceiveInProgress = false;
   self->TransmitInProgress = false;

   self->ReceiveBufferAvail = false;

   self->TransmitBuffer = 0;
   self->ReceiveBuffer = 0;

   self->ReceiveBitCounter = 0;
   self->TransmitBitCounter = 0;

   self->Mode = 0;
   self->Control = 0;
   self->Baudrate = 0;

   for(i = 0; i < 8; i++)
   {
      (self->Devices[i])->vt->Power((self->Devices[i]));
      (self->DevicesMC[i])->vt->Power((self->DevicesMC[i]));
   }

   self->istatus = false;
}

void FrontIO_UpdateInput(FrontIO *self_)
{
   FrontIO *self = self_;

   int i;
   for(i = 0; i < 8; i++)
      (self->Devices[i])->vt->UpdateInput((self->Devices[i]), self->DeviceData[i]);
}

void FrontIO_SetInput(FrontIO *self_, unsigned int port, const char *type, void *ptr)
{
   FrontIO *self = self_;

   free(self->Devices[port]);
   self->Devices[port] = NULL;

   if(port < 2)
      self->irq10_pulse_ts[port] = PSX_EVENT_MAXTS;

   if(!strcmp(type, "gamepad") || !strcmp(type, "dancepad"))
      self->Devices[port] = Device_Gamepad_Create();
   else if(!strcmp(type, "dualanalog"))
      self->Devices[port] = Device_DualAnalog_Create(false);
   else if(!strcmp(type, "analogjoy"))
      self->Devices[port] = Device_DualAnalog_Create(true);
   else if(!strcmp(type, "dualshock"))
   {
      char name[256];
      snprintf(name, 256, "DualShock on port %u", port + 1);
      self->Devices[port] = Device_DualShock_Create(name);
   }
   else if(!strcmp(type, "mouse"))
      self->Devices[port] = Device_Mouse_Create();
   else if(!strcmp(type, "negcon"))
      self->Devices[port] = Device_neGcon_Create();
   else if(!strcmp(type, "negconrumble"))
   {
      char name[256];
      snprintf(name, 256, "neGcon Rumble on port %u", port + 1);
      self->Devices[port] = Device_neGconRumble_Create(name);
   }
   else if(!strcmp(type, "guncon"))
      self->Devices[port] = Device_GunCon_Create();
   else if(!strcmp(type, "justifier"))
      self->Devices[port] = Device_Justifier_Create();
   else
      self->Devices[port] = InputDevice_New();

   (self->Devices[port])->vt->SetAMCT((self->Devices[port]), self->amct_enabled);
   (self->Devices[port])->vt->SetCrosshairsCursor((self->Devices[port]), self->chair_cursor[port]);
   (self->Devices[port])->vt->SetCrosshairsColor((self->Devices[port]), self->chair_colors[port]);
   self->DeviceData[port] = ptr;

   FrontIO_MapDevicesToPorts(self_);
}

uint64_t FrontIO_GetMemcardDirtyCount(FrontIO *self_, unsigned int which)
{
   FrontIO *self = self_;

 assert(which < 8);

 return((self->DevicesMC[which])->vt->GetNVDirtyCount((self->DevicesMC[which])));
}

void FrontIO_LoadMemcard(FrontIO *self_, unsigned int which)
{
   FrontIO *self = self_;

   assert(which < 8);

   if((self->DevicesMC[which])->vt->GetNVSize((self->DevicesMC[which])))
   {
      (self->DevicesMC[which])->vt->WriteNV((self->DevicesMC[which]), (self->DevicesMC[which])->vt->GetNVData((self->DevicesMC[which])), 0, (1 << 17));
      (self->DevicesMC[which])->vt->ResetNVDirtyCount((self->DevicesMC[which]));		/*  There's no need to rewrite the file if it's the same data. */
   }
}

void FrontIO_LoadMemcardFromPath(FrontIO *self_, unsigned int which, const char *path, bool force_load)
{
   FrontIO *self = self_;

 assert(which < 8);

 if((self->DevicesMC[which])->vt->GetNVSize((self->DevicesMC[which])))
 {
    RFILE *mf = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ, 
          RETRO_VFS_FILE_ACCESS_HINT_NONE);

    if (!mf)
    {
       if (force_load)
       {
          Device_Memcard_Power(self->DevicesMC[which]);
          Device_Memcard_Format(self->DevicesMC[which]);
       }
       return;
    }

    Device_Memcard_Power(self->DevicesMC[which]);

    filestream_read(mf, (self->DevicesMC[which])->vt->GetNVData((self->DevicesMC[which])), (1 << 17));

    (self->DevicesMC[which])->vt->WriteNV((self->DevicesMC[which]), (self->DevicesMC[which])->vt->GetNVData((self->DevicesMC[which])), 0, (1 << 17));
    (self->DevicesMC[which])->vt->ResetNVDirtyCount((self->DevicesMC[which]));		/*  There's no need to rewrite the file if it's the same data. */

    filestream_close(mf);
 }
}

void FrontIO_SaveMemcard(FrontIO *self_, unsigned int which)
{
   FrontIO *self = self_;

 assert(which < 8);

 if((self->DevicesMC[which])->vt->GetNVSize((self->DevicesMC[which])) && (self->DevicesMC[which])->vt->GetNVDirtyCount((self->DevicesMC[which])))
 {
  (self->DevicesMC[which])->vt->ReadNV((self->DevicesMC[which]), (self->DevicesMC[which])->vt->GetNVData((self->DevicesMC[which])), 0, (1 << 17));
  (self->DevicesMC[which])->vt->ResetNVDirtyCount((self->DevicesMC[which]));
 }
}

void FrontIO_SaveMemcardToPath(FrontIO *self_, unsigned int which, const char *path, bool force_save)
{
   FrontIO *self = self_;

 assert(which < 8);

 if((self->DevicesMC[which])->vt->GetNVSize((self->DevicesMC[which])) && (force_save || (self->DevicesMC[which])->vt->GetNVDirtyCount((self->DevicesMC[which]))))
 {
    RFILE *mf = filestream_open(path, 
          RETRO_VFS_FILE_ACCESS_WRITE,
          RETRO_VFS_FILE_ACCESS_HINT_NONE);

    if (!mf)
       return;

    (self->DevicesMC[which])->vt->ReadNV((self->DevicesMC[which]), (self->DevicesMC[which])->vt->GetNVData((self->DevicesMC[which])), 0, (1 << 17));
    filestream_write(mf, (self->DevicesMC[which])->vt->GetNVData((self->DevicesMC[which])), (1 << 17));

    filestream_close(mf);	/*  Call before resetting the NV dirty count! */

    (self->DevicesMC[which])->vt->ResetNVDirtyCount((self->DevicesMC[which]));
 }
}

void FrontIO_GPULineHook(FrontIO *self_, const int32_t timestamp, const int32_t line_timestamp, bool vsync, uint32_t *pixels, const unsigned width, const unsigned pix_clock_offset, const unsigned pix_clock, const unsigned pix_clock_divider, const unsigned surf_pitchinpix, const unsigned upscale_factor)
{
   unsigned i;
   FrontIO *self = self_;

   FrontIO_Update(self_, timestamp);

   for (i = 0; i < 8; i++)
   {
      int32_t plts = (self->Devices[i])->vt->GPULineHook((self->Devices[i]), line_timestamp, vsync, pixels, width, pix_clock_offset, pix_clock, pix_clock_divider, surf_pitchinpix, upscale_factor);

      if(i < 2)
      {
         self->irq10_pulse_ts[i] = plts;

         if(self->irq10_pulse_ts[i] <= timestamp)
         {
            self->irq10_pulse_ts[i] = PSX_EVENT_MAXTS;
            IRQ_Assert(IRQ_PIO, true);
            IRQ_Assert(IRQ_PIO, false);
         }
      }
   }

   /*  */
   /*  Draw crosshairs in a separate pass so the crosshairs won't mess up the color evaluation of later lightun GPULineHook()s. */
   /*  */
   if(pixels && pix_clock)
   {
      for (i = 0; i < 8; i++)
      {
         InputDevice_DrawCrosshairs(self->Devices[i], pixels, width, pix_clock, surf_pitchinpix, upscale_factor);
      }
   }

   PSX_SetEventNT(PSX_EVENT_FIO, FrontIO_CalcNextEventTS(self_, timestamp, 0x10000000));
}

/* ===========================================================================
 *  Merged from mednafen/psx/input/gamepad.cpp
 * =========================================================================== */

typedef struct InputDevice_Gamepad InputDevice_Gamepad;
struct InputDevice_Gamepad
{
   InputDevice base;
         bool dtr;
         uint8_t buttons[2];
         int32_t command_phase;
         uint32_t bitpos;
         uint8_t receive_buffer;
         uint8_t command;
         uint8_t transmit_buffer[3];
         uint32_t transmit_pos;
         uint32_t transmit_count;
};


static void InputDevice_Gamepad_Ctor(InputDevice *self_)
{
   (void)self_;

   InputDevice_Gamepad_Power(self_);
}



static void InputDevice_Gamepad_Power(InputDevice *self_)
{
   InputDevice_Gamepad *self = (InputDevice_Gamepad *)self_;

   self->dtr = 0;

   self->buttons[0] = self->buttons[1] = 0;

   self->command_phase = 0;

   self->bitpos = 0;

   self->receive_buffer = 0;

   self->command = 0;

   memset(self->transmit_buffer, 0, sizeof(self->transmit_buffer));

   self->transmit_pos = 0;
   self->transmit_count = 0;
}

static int InputDevice_Gamepad_StateAction(InputDevice *self_, StateMem* sm, int load, int data_only, const char* section_name)
{
   InputDevice_Gamepad *self = (InputDevice_Gamepad *)self_;

   SFORMAT StateRegs[] =
   {
      SFVAR(self->dtr),

      SFARRAY(self->buttons, sizeof(self->buttons)),

      SFVAR(self->command_phase),
      SFVAR(self->bitpos),
      SFVAR(self->receive_buffer),

      SFVAR(self->command),

      SFARRAY(self->transmit_buffer, sizeof(self->transmit_buffer)),
      SFVAR(self->transmit_pos),
      SFVAR(self->transmit_count),

      SFEND
   };
   int ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, section_name);

   if(load)
   {
      if(((uint64_t)self->transmit_pos + self->transmit_count) > sizeof(self->transmit_buffer))
      {
         self->transmit_pos = 0;
         self->transmit_count = 0;
      }
   }

   return(ret);
}


static void InputDevice_Gamepad_UpdateInput(InputDevice *self_, const void *data)
{
   InputDevice_Gamepad *self = (InputDevice_Gamepad *)self_;

   uint8_t *d8 = (uint8_t *)data;

   self->buttons[0] = d8[0];
   self->buttons[1] = d8[1];
}


static void InputDevice_Gamepad_SetDTR(InputDevice *self_, bool new_dtr)
{
   InputDevice_Gamepad *self = (InputDevice_Gamepad *)self_;

   if(!self->dtr && new_dtr)
   {
      self->command_phase = 0;
      self->bitpos = 0;
      self->transmit_pos = 0;
      self->transmit_count = 0;
   }
   else if(self->dtr && !new_dtr)
   {
      /* if(bitpos || transmit_count) */
      /*  printf("[PAD] Abort communication!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"); */
   }

   self->dtr = new_dtr;
}

static bool InputDevice_Gamepad_GetDSR(InputDevice *self_)
{
   InputDevice_Gamepad *self = (InputDevice_Gamepad *)self_;

   if(!self->dtr)
      return(0);

   if(!self->bitpos && self->transmit_count)
      return(1);

   return(0);
}

static bool InputDevice_Gamepad_Clock(InputDevice *self_, bool TxD, int32_t *dsr_pulse_delay)
{
   InputDevice_Gamepad *self = (InputDevice_Gamepad *)self_;

   bool ret = 1;

   *dsr_pulse_delay = 0;

   if(!self->dtr)
      return(1);

   if(self->transmit_count)
      ret = (self->transmit_buffer[self->transmit_pos] >> self->bitpos) & 1;

   self->receive_buffer &= ~(1 << self->bitpos);
   self->receive_buffer |= TxD << self->bitpos;
   self->bitpos = (self->bitpos + 1) & 0x7;

   if(!self->bitpos)
   {
      /* printf("[PAD] Receive: %02x -- command_phase=%d\n", receive_buffer, command_phase); */

      if(self->transmit_count)
      {
         self->transmit_pos++;
         self->transmit_count--;
      }


      switch(self->command_phase)
      {
         case 0:
            if(self->receive_buffer != 0x01)
               self->command_phase = -1;
            else
            {
               self->transmit_buffer[0] = 0x41;
               self->transmit_pos = 0;
               self->transmit_count = 1;
               self->command_phase++;
            }
            break;

         case 1:
            self->command = self->receive_buffer;
            self->command_phase++;

            self->transmit_buffer[0] = 0x5A;

            /* if(command != 0x42) */
            /*  fprintf(stderr, "Gamepad unhandled command: 0x%02x\n", command); */
            /* assert(command == 0x42); */
            if(self->command == 0x42)
            {
               /* printf("PAD COmmand 0x42, sl=%u\n", GPU->GetScanlineNum()); */

               self->transmit_buffer[1] = 0xFF ^ self->buttons[0];
               self->transmit_buffer[2] = 0xFF ^ self->buttons[1];
               self->transmit_pos = 0;
               self->transmit_count = 3;
            }
            else
            {
               self->command_phase = -1;
               self->transmit_buffer[1] = 0;
               self->transmit_buffer[2] = 0;
               self->transmit_pos = 0;
               self->transmit_count = 0;
            }
            break;

      }
   }

   if(!self->bitpos && self->transmit_count)
      *dsr_pulse_delay = 0x40; /* 0x100; */

   return(ret);
}





/* ===========================================================================
 *  Merged from mednafen/psx/input/dualanalog.cpp
 * =========================================================================== */

typedef struct InputDevice_DualAnalog InputDevice_DualAnalog;
struct InputDevice_DualAnalog
{
   InputDevice base;
         bool joystick_mode;
         bool dtr;
         uint8_t buttons[2];
         uint8_t axes[2][2];
         int32_t command_phase;
         uint32_t bitpos;
         uint8_t receive_buffer;
         uint8_t command;
         uint8_t transmit_buffer[8];
         uint32_t transmit_pos;
         uint32_t transmit_count;
};


static void InputDevice_DualAnalog_Ctor(InputDevice *self_, bool joystick_mode_)
{
   (void)self_;

   InputDevice_DualAnalog_Power(self_);
}



static void InputDevice_DualAnalog_Power(InputDevice *self_)
{
   InputDevice_DualAnalog *self = (InputDevice_DualAnalog *)self_;

   self->dtr = 0;

   self->buttons[0] = self->buttons[1] = 0;

   self->command_phase = 0;

   self->bitpos = 0;

   self->receive_buffer = 0;

   self->command = 0;

   memset(self->transmit_buffer, 0, sizeof(self->transmit_buffer));

   self->transmit_pos = 0;
   self->transmit_count = 0;
}

static int InputDevice_DualAnalog_StateAction(InputDevice *self_, StateMem* sm, int load, int data_only, const char* section_name)
{
   InputDevice_DualAnalog *self = (InputDevice_DualAnalog *)self_;

   SFORMAT StateRegs[] =
   {
      SFVAR(self->dtr),

      SFARRAY(self->buttons, sizeof(self->buttons)),
      SFARRAY(&self->axes[0][0], sizeof(self->axes)),

      SFVAR(self->command_phase),
      SFVAR(self->bitpos),
      SFVAR(self->receive_buffer),

      SFVAR(self->command),

      SFARRAY(self->transmit_buffer, sizeof(self->transmit_buffer)),
      SFVAR(self->transmit_pos),
      SFVAR(self->transmit_count),

      SFEND
   };
   int ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, section_name);

   if(load)
   {
      if(((uint64_t)self->transmit_pos + self->transmit_count) > sizeof(self->transmit_buffer))
      {
         self->transmit_pos = 0;
         self->transmit_count = 0;
      }
   }

   return(ret);
}

static void InputDevice_DualAnalog_UpdateInput(InputDevice *self_, const void *data)
{
   int stick, axis;
   InputDevice_DualAnalog *self = (InputDevice_DualAnalog *)self_;

   uint8_t *d8 = (uint8_t *)data;

   self->buttons[0] = d8[0];
   self->buttons[1] = d8[1];

   for (stick = 0; stick < 2; stick++)
   {
      for (axis = 0; axis < 2; axis++)
      {
         int32_t tmp;
         const uint8_t *_p4 = (const uint8_t *)data + stick * 16 + axis * 8 + 4;
         const uint8_t *_p8 = (const uint8_t *)data + stick * 16 + axis * 8 + 8;
         uint32_t _v4, _v8;

         /* revert to 0.9.33, should be fixed on libretro side instead */
         /* tmp = 32768 + MDFN_de16lsb(&aba[0]) - ((int32)MDFN_de16lsb(&aba[2]) * 32768 / 32767); */

#ifdef MSB_FIRST
         _v4 = (uint32_t)_p4[0] | ((uint32_t)_p4[1] << 8) | ((uint32_t)_p4[2] << 16) | ((uint32_t)_p4[3] << 24);
         _v8 = (uint32_t)_p8[0] | ((uint32_t)_p8[1] << 8) | ((uint32_t)_p8[2] << 16) | ((uint32_t)_p8[3] << 24);
#else
         memcpy(&_v4, _p4, 4);
         memcpy(&_v8, _p8, 4);
#endif
         tmp = 32768 + _v4 - ((int32_t)_v8 * 32768 / 32767);
         tmp >>= 8;

         self->axes[stick][axis] = tmp;
      }
   }

   /* printf("%d %d %d %d\n", axes[0][0], axes[0][1], axes[1][0], axes[1][1]); */

}


static void InputDevice_DualAnalog_SetDTR(InputDevice *self_, bool new_dtr)
{
   InputDevice_DualAnalog *self = (InputDevice_DualAnalog *)self_;

   if(!self->dtr && new_dtr)
   {
      self->command_phase = 0;
      self->bitpos = 0;
      self->transmit_pos = 0;
      self->transmit_count = 0;
   }
   else if(self->dtr && !new_dtr)
   {
      /* if(bitpos || transmit_count) */
      /*  printf("[PAD] Abort communication!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"); */
   }

   self->dtr = new_dtr;
}

static bool InputDevice_DualAnalog_GetDSR(InputDevice *self_)
{
   InputDevice_DualAnalog *self = (InputDevice_DualAnalog *)self_;

   if(!self->dtr)
      return(0);

   if(!self->bitpos && self->transmit_count)
      return(1);

   return(0);
}

static bool InputDevice_DualAnalog_Clock(InputDevice *self_, bool TxD, int32_t *dsr_pulse_delay)
{
   InputDevice_DualAnalog *self = (InputDevice_DualAnalog *)self_;

   bool ret = 1;

   *dsr_pulse_delay = 0;

   if(!self->dtr)
      return(1);

   if(self->transmit_count)
      ret = (self->transmit_buffer[self->transmit_pos] >> self->bitpos) & 1;

   self->receive_buffer &= ~(1 << self->bitpos);
   self->receive_buffer |= TxD << self->bitpos;
   self->bitpos = (self->bitpos + 1) & 0x7;

   if(!self->bitpos)
   {
      /* printf("[PAD] Receive: %02x -- command_phase=%d\n", receive_buffer, command_phase); */

      if(self->transmit_count)
      {
         self->transmit_pos++;
         self->transmit_count--;
      }


      switch(self->command_phase)
      {
         case 0:
            if(self->receive_buffer != 0x01)
               self->command_phase = -1;
            else
            {
               self->transmit_buffer[0] = self->joystick_mode ? 0x53 : 0x73;
               self->transmit_pos = 0;
               self->transmit_count = 1;
               self->command_phase++;
            }
            break;

         case 1:
            self->command = self->receive_buffer;
            self->command_phase++;

            self->transmit_buffer[0] = 0x5A;

            /* if(command != 0x42) */
            /*  fprintf(stderr, "Gamepad unhandled command: 0x%02x\n", command); */

            if(self->command == 0x42)
            {
               self->transmit_buffer[1] = 0xFF ^ self->buttons[0];
               self->transmit_buffer[2] = 0xFF ^ self->buttons[1];
               self->transmit_buffer[3] = self->axes[0][0];
               self->transmit_buffer[4] = self->axes[0][1];
               self->transmit_buffer[5] = self->axes[1][0];
               self->transmit_buffer[6] = self->axes[1][1];
               self->transmit_pos = 0;
               self->transmit_count = 7;
            }
            else
            {
               self->command_phase = -1;
               self->transmit_buffer[1] = 0;
               self->transmit_buffer[2] = 0;
               self->transmit_pos = 0;
               self->transmit_count = 0;
            }
            break;
         case 2:
            /* if(receive_buffer) */
            /*  printf("%d: %02x\n", 7 - transmit_count, receive_buffer); */
            break;
      }
   }

   if(!self->bitpos && self->transmit_count)
      *dsr_pulse_delay = 0x40; /* 0x100; */

   return(ret);
}


/*  Not sure if all these buttons are named correctly! */


/* ===========================================================================
 *  Merged from mednafen/psx/input/dualshock.cpp
 * =========================================================================== */

/*
   TODO:
	If we ever call Update() more than once per video frame(IE 50/60Hz), we'll need to add debounce logic to the analog mode button evaluation code.
*/

/* Notes:

     Both DA and DS style rumblings work in both analog and digital modes.

     Regarding getting Dual Shock style rumble working, Sony is evil and/or mean.  The owl tells me to burn Sony with boiling oil.

     To enable Dual Shock-style rumble, the game has to at least enter MAD MUNCHKINS MODE with command 0x43, and send the appropriate data(not the actual rumble type data per-se)
     with command 0x4D.

     DualAnalog-style rumble support seems to be borked until power loss if MAD MUNCHKINS MODE is even entered once...investigate further.

     Command 0x44 in MAD MUNCHKINS MODE can turn on/off analog mode(and the light with it).

     Command 0x42 in MAD MUNCHKINS MODE will return the analog mode style gamepad data, even when analog mode is off.  In combination with command 0x44, this could hypothetically
     be used for using the light in the gamepad as some kind of game mechanic).

     Dual Analog-style rumble notes(some of which may apply to DS too):
	Rumble appears to stop if you hold DTR active(TODO: for how long? instant?). (TODO: investigate if it's still stopped even if a memory card device number is sent.  It may be, since rumble may
							      cause excessive current draw in combination with memory card access)

	Rumble will work even if you interrupt the communication process after you've sent the rumble data(via command 0x42).
		Though if you interrupt it when you've only sent partial rumble data, dragons will eat you and I don't know(seems to have timing-dependent or random effects or something;
	        based on VERY ROUGH testing).
*/

typedef struct InputDevice_DualShock InputDevice_DualShock;
struct InputDevice_DualShock
{
   InputDevice base;
         bool cur_ana_button_state;
         bool prev_ana_button_state;
         int64_t combo_anatoggle_counter;
         bool da_rumble_compat;
         bool analog_mode;
         bool analog_mode_locked;
         bool mad_munchkins;
         uint8_t rumble_magic[6];
         uint8_t rumble_param[2];
         bool dtr;
         uint8_t buttons[2];
         uint8_t axes[2][2];
         int32_t command_phase;
         uint32_t bitpos;
         uint8_t receive_buffer;
         uint8_t command;
         uint8_t transmit_buffer[8];
         uint32_t transmit_pos;
         uint32_t transmit_count;
         bool am_prev_info;
         bool aml_prev_info;
         char gp_name[64];
         int32_t lastts;
         bool amct_enabled;
};


static void InputDevice_DualShock_Ctor(InputDevice *self_, const char *name)
{
   InputDevice_DualShock *self = (InputDevice_DualShock *)self_;

   strlcpy(self->gp_name, name, sizeof(self->gp_name));
   InputDevice_DualShock_Power(self_);
   self->am_prev_info = self->analog_mode;
   self->aml_prev_info = self->analog_mode_locked;
   self->amct_enabled = false;
}



static void InputDevice_DualShock_Update(InputDevice *self_, const int32_t timestamp)
{
   InputDevice_DualShock *self = (InputDevice_DualShock *)self_;

   self->lastts = timestamp;
}

static void InputDevice_DualShock_ResetTS(InputDevice *self_)
{
   InputDevice_DualShock *self = (InputDevice_DualShock *)self_;

   /* printf("%lld\n", combo_anatoggle_counter); */
   if(self->combo_anatoggle_counter >= 0)
      self->combo_anatoggle_counter += self->lastts;
   self->lastts = 0;
}

static void InputDevice_DualShock_SetAMCT(InputDevice *self_, bool enabled)
{
   InputDevice_DualShock *self = (InputDevice_DualShock *)self_;

   self->amct_enabled = enabled;
   if(self->amct_enabled)
      self->analog_mode = setting_apply_analog_default;
   else
      self->analog_mode = true;

   self->am_prev_info = self->analog_mode;
}

/*  */
/*  This simulates the behavior of the actual DualShock(analog toggle button evaluation is suspended while DTR is active). */
/*  Call in Update(), and whenever dtr goes inactive in the port access code. */
static void InputDevice_DualShock_CheckManualAnaModeChange(InputDevice *self_)
{
   InputDevice_DualShock *self = (InputDevice_DualShock *)self_;

   if(!self->dtr)
   {
      bool need_mode_toggle = false;

      if(self->amct_enabled)
      {
         if(self->buttons[0] == analog_combo[0] && self->buttons[1] == analog_combo[1])
         {
            if(self->combo_anatoggle_counter == -1)
               self->combo_anatoggle_counter = 0;
            else if(self->combo_anatoggle_counter >= (44100 * (768 * analog_combo_hold)))
            {
               need_mode_toggle = true;
               self->combo_anatoggle_counter = -2;
            }
         }
         else
            self->combo_anatoggle_counter = -1;
      }  
      else
      {
         self->combo_anatoggle_counter = -1;
         if(self->cur_ana_button_state && (self->cur_ana_button_state != self->prev_ana_button_state))
         {
            need_mode_toggle = true;
         }
      }

      if(need_mode_toggle)
      {
         if(self->analog_mode_locked)
            {
				
			}
         else
            self->analog_mode = !self->analog_mode;
      }

      self->prev_ana_button_state = self->cur_ana_button_state; 	/*  Don't move this outside of the if(!dtr) block! */
   }
}

static void InputDevice_DualShock_Power(InputDevice *self_)
{
   InputDevice_DualShock *self = (InputDevice_DualShock *)self_;

   self->combo_anatoggle_counter = -2;
   self->lastts = 0;
   /*  */
   /*  */

   self->dtr = 0;

   self->buttons[0] = self->buttons[1] = 0;

   self->command_phase = 0;

   self->bitpos = 0;

   self->receive_buffer = 0;

   self->command = 0;

   memset(self->transmit_buffer, 0, sizeof(self->transmit_buffer));

   self->transmit_pos = 0;
   self->transmit_count = 0;

   self->analog_mode_locked = false;

   self->mad_munchkins = false;
   memset(self->rumble_magic, 0xFF, sizeof(self->rumble_magic));
   memset(self->rumble_param, 0, sizeof(self->rumble_param));

   self->da_rumble_compat = true;

   self->prev_ana_button_state = false;
}

static int InputDevice_DualShock_StateAction(InputDevice *self_, StateMem* sm, int load, int data_only, const char* section_name)
{
   InputDevice_DualShock *self = (InputDevice_DualShock *)self_;

   SFORMAT StateRegs[] =
   {
      SFVAR(self->cur_ana_button_state),
      SFVAR(self->prev_ana_button_state),
      SFVAR(self->combo_anatoggle_counter),

      SFVAR(self->da_rumble_compat),

      SFVAR(self->analog_mode),
      SFVAR(self->analog_mode_locked),

      SFVAR(self->mad_munchkins),
      SFARRAY(self->rumble_magic, sizeof(self->rumble_magic)),

      SFARRAY(self->rumble_param, sizeof(self->rumble_param)),

      SFVAR(self->dtr),

      SFARRAY(self->buttons, sizeof(self->buttons)),
      SFARRAY(&self->axes[0][0], sizeof(self->axes)),

      SFVAR(self->command_phase),
      SFVAR(self->bitpos),
      SFVAR(self->receive_buffer),

      SFVAR(self->command),

      SFARRAY(self->transmit_buffer, sizeof(self->transmit_buffer)),
      SFVAR(self->transmit_pos),
      SFVAR(self->transmit_count),

      SFEND
   };
   int ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, section_name);

   if(load)
   {
      if(((uint64_t)self->transmit_pos + self->transmit_count) > sizeof(self->transmit_buffer))
      {
         self->transmit_pos = 0;
         self->transmit_count = 0;
      }
   }

   return(ret);
}

static void InputDevice_DualShock_UpdateInput(InputDevice *self_, const void *data)
{
   int stick, axis;
   InputDevice_DualShock *self = (InputDevice_DualShock *)self_;

   uint8_t *d8 = (uint8_t *)data;

   self->buttons[0] = d8[0];
   self->buttons[1] = d8[1];
   self->cur_ana_button_state = d8[2] & 0x01;

   for (stick = 0; stick < 2; stick++)
   {
      for (axis = 0; axis < 2; axis++)
      {
         int32_t tmp;
         const uint8_t *_p4 = (const uint8_t *)data + stick * 16 + axis * 8 + 4;
         const uint8_t *_p8 = (const uint8_t *)data + stick * 16 + axis * 8 + 8;
         uint32_t _v4, _v8;

         /* revert to 0.9.33, should be fixed on libretro side instead */
         /* tmp = 32767 + MDFN_de16lsb(&aba[0]) - MDFN_de16lsb(&aba[2]); */
         /* tmp = (tmp * 0x100) / 0xFFFF; */

#ifdef MSB_FIRST
         _v4 = (uint32_t)_p4[0] | ((uint32_t)_p4[1] << 8) | ((uint32_t)_p4[2] << 16) | ((uint32_t)_p4[3] << 24);
         _v8 = (uint32_t)_p8[0] | ((uint32_t)_p8[1] << 8) | ((uint32_t)_p8[2] << 16) | ((uint32_t)_p8[3] << 24);
#else
         memcpy(&_v4, _p4, 4);
         memcpy(&_v8, _p8, 4);
#endif
         tmp = 32768 + _v4 - ((int32_t)_v8 * 32768 / 32767);
         tmp >>= 8;
         self->axes[stick][axis] = tmp;
      }
   }

   /* printf("%3d:%3d, %3d:%3d\n", axes[0][0], axes[0][1], axes[1][0], axes[1][1]); */

   /* printf("RUMBLE: 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n", rumble_magic[0], rumble_magic[1], rumble_magic[2], rumble_magic[3], rumble_magic[4], rumble_magic[5]); */
   /* printf("%d, 0x%02x 0x%02x\n", da_rumble_compat, rumble_param[0], rumble_param[1]); */
   if(self->da_rumble_compat == false)
   {
      uint8_t sneaky_weaky = 0;
      uint32_t _rv;

      if(self->rumble_param[0] == 0x01)
         sneaky_weaky = 0xFF;

      /* revert to 0.9.33, should be fixed on libretro side instead */
      /* MDFN_en16lsb(rumb_dp, (sneaky_weaky << 0) | (rumble_param[1] << 8)); */

      _rv = (uint32_t)((sneaky_weaky << 0) | (self->rumble_param[1] << 8));
#ifdef MSB_FIRST
      d8[4 + 32 + 0] = _rv;
      d8[4 + 32 + 1] = _rv >> 8;
      d8[4 + 32 + 2] = _rv >> 16;
      d8[4 + 32 + 3] = _rv >> 24;
#else
      memcpy(&d8[4 + 32 + 0], &_rv, 4);
#endif
   }
   else
   {
      uint8_t sneaky_weaky = 0;
      uint32_t _rv;

      if(((self->rumble_param[0] & 0xC0) == 0x40) && ((self->rumble_param[1] & 0x01) == 0x01))
         sneaky_weaky = 0xFF;

      /* revert to 0.9.33, should be fixed on libretro side instead */
      /* MDFN_en16lsb(rumb_dp, sneaky_weaky << 0); */
      _rv = (uint32_t)(sneaky_weaky << 0);
#ifdef MSB_FIRST
      d8[4 + 32 + 0] = _rv;
      d8[4 + 32 + 1] = _rv >> 8;
      d8[4 + 32 + 2] = _rv >> 16;
      d8[4 + 32 + 3] = _rv >> 24;
#else
      memcpy(&d8[4 + 32 + 0], &_rv, 4);
#endif
   }

   /* printf("%d %d %d %d\n", axes[0][0], axes[0][1], axes[1][0], axes[1][1]); */

   /*  */
   /*  */
   /*  */
   InputDevice_DualShock_CheckManualAnaModeChange(self_);

   if(self->am_prev_info != self->analog_mode)
      osd_message(2, RETRO_LOG_INFO,
            RETRO_MESSAGE_TARGET_OSD, RETRO_MESSAGE_TYPE_NOTIFICATION_ALT,
            "%s: %s Mode",
            self->gp_name, self->analog_mode ? "Analog" : "Digital");

   self->aml_prev_info = self->analog_mode_locked;
   self->am_prev_info = self->analog_mode;
}


static void InputDevice_DualShock_SetDTR(InputDevice *self_, bool new_dtr)
{
   InputDevice_DualShock *self = (InputDevice_DualShock *)self_;

   const bool old_dtr = self->dtr;
   self->dtr = new_dtr;	/*  Set it to new state before we call InputDevice_DualShock_CheckManualAnaModeChange(self_). */

   if(!old_dtr && self->dtr)
   {
      self->command_phase = 0;
      self->bitpos = 0;
      self->transmit_pos = 0;
      self->transmit_count = 0;
   }
   else if(old_dtr && !self->dtr)
   {
      InputDevice_DualShock_CheckManualAnaModeChange(self_);
      /* if(bitpos || transmit_count) */
      /*  printf("[PAD] Abort communication!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"); */
   }
}

static bool InputDevice_DualShock_GetDSR(InputDevice *self_)
{
   InputDevice_DualShock *self = (InputDevice_DualShock *)self_;

   if(!self->dtr)
      return(0);

   if(!self->bitpos && self->transmit_count)
      return(1);

   return(0);
}

static bool InputDevice_DualShock_Clock(InputDevice *self_, bool TxD, int32_t *dsr_pulse_delay)
{
   InputDevice_DualShock *self = (InputDevice_DualShock *)self_;

   bool ret = 1;

   *dsr_pulse_delay = 0;

   if(!self->dtr)
      return(1);

   if(self->transmit_count)
      ret = (self->transmit_buffer[self->transmit_pos] >> self->bitpos) & 1;

   self->receive_buffer &= ~(1 << self->bitpos);
   self->receive_buffer |= TxD << self->bitpos;
   self->bitpos = (self->bitpos + 1) & 0x7;

   if(!self->bitpos)
   {
      /* if(command == 0x44) */
      /* if(command == 0x4D) //mad_munchkins) // || command == 0x43) */
      /*  fprintf(stderr, "[PAD] Receive: %02x -- command=%02x, command_phase=%d, transmit_pos=%d\n", receive_buffer, command, command_phase, transmit_pos); */

      if(self->transmit_count)
      {
         self->transmit_pos++;
         self->transmit_count--;
      }

      switch(self->command_phase)
      {
         case 0:
            if(self->receive_buffer != 0x01)
               self->command_phase = -1;
            else
            {
               if(self->mad_munchkins)
               {
                  self->transmit_buffer[0] = 0xF3;
                  self->transmit_pos = 0;
                  self->transmit_count = 1;
                  self->command_phase = 101;
               }
               else
               {
                  self->transmit_buffer[0] = self->analog_mode ? 0x73 : 0x41;
                  self->transmit_pos = 0;
                  self->transmit_count = 1;
                  self->command_phase++;
               }
            }
            break;

         case 1:
            self->command = self->receive_buffer;
            self->command_phase++;

            self->transmit_buffer[0] = 0x5A;

            /* fprintf(stderr, "Gamepad command: 0x%02x\n", command); */
            /* if(command != 0x42 && command != 0x43) */
            /*  fprintf(stderr, "Gamepad unhandled command: 0x%02x\n", command); */

            if(self->command == 0x42)
            {
               self->transmit_buffer[0] = 0x5A;
               self->transmit_pos = 0;
               self->transmit_count = 1;
               self->command_phase = (self->command << 8) | 0x00;
            }
            else if(self->command == 0x43)
            {
               self->transmit_pos = 0;
               if(self->analog_mode)
               {
                  self->transmit_buffer[1] = 0xFF ^ self->buttons[0];
                  self->transmit_buffer[2] = 0xFF ^ self->buttons[1];
                  self->transmit_buffer[3] = self->axes[0][0];
                  self->transmit_buffer[4] = self->axes[0][1];
                  self->transmit_buffer[5] = self->axes[1][0];
                  self->transmit_buffer[6] = self->axes[1][1];
                  self->transmit_count = 7;
               }
               else
               {
                  self->transmit_buffer[1] = 0xFF ^ self->buttons[0];
                  self->transmit_buffer[2] = 0xFF ^ self->buttons[1];
                  self->transmit_count = 3;
               }
            }
            else
            {
               self->command_phase = -1;
               self->transmit_buffer[1] = 0;
               self->transmit_buffer[2] = 0;
               self->transmit_pos = 0;
               self->transmit_count = 0;
            }
            break;

         case 2:
            {
               if(self->command == 0x43 && self->transmit_pos == 2 && (self->receive_buffer == 0x01))
               {
                  /* fprintf(stderr, "Mad Munchkins mode entered!\n"); */
                  self->mad_munchkins = true;

                  if(self->da_rumble_compat)
                  {
                     self->rumble_param[0] = 0;
                     self->rumble_param[1] = 0;
                     self->da_rumble_compat = false;
                  }
                  self->command_phase = -1;
               }
            }
            break;

         case 101:
            self->command = self->receive_buffer;

            /* fprintf(stderr, "Mad Munchkins DualShock command: 0x%02x\n", command); */

            if(self->command >= 0x40 && self->command <= 0x4F)
            {
               self->transmit_buffer[0] = 0x5A;
               self->transmit_pos = 0;
               self->transmit_count = 1;
               self->command_phase = (self->command << 8) | 0x00;
            }
            else
            {
               self->transmit_count = 0;
               self->command_phase = -1;
            }
            break;

            /************************/
            /* MMMode 1, Command 0x40 */
            /************************/
         case 0x4000:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0; /**/ self->transmit_pos = 0; self->transmit_count = 1; /**/
               self->command_phase++;
            }
            else
               self->command_phase = -1;
            break;

         case 0x4001:
            self->transmit_buffer[0] = 0x00;
            self->transmit_buffer[1] = 0x00;
            self->transmit_buffer[2] = 0x00;
            self->transmit_buffer[3] = 0x00;
            self->transmit_buffer[4] = 0x00;
            self->transmit_pos = 0;
            self->transmit_count = 5;
            self->command_phase = -1;
            break;


            /************************/
            /* MMMode 1, Command 0x41 */
            /************************/
         case 0x4100:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0; /**/ self->transmit_pos = 0; self->transmit_count = 1; /**/
               self->command_phase++;
            }
            else
               self->command_phase = -1;
            break;

         case 0x4101:
            self->transmit_buffer[0] = 0x00;
            self->transmit_buffer[1] = 0x00;
            self->transmit_buffer[2] = 0x00;
            self->transmit_buffer[3] = 0x00;
            self->transmit_buffer[4] = 0x00;
            self->transmit_pos = 0;
            self->transmit_count = 5;
            self->command_phase = -1;
            break;

            /**************************/
            /* MMMode 0&1, Command 0x42 */
            /**************************/
         case 0x4200:
            self->transmit_pos = 0;
            if(self->analog_mode || self->mad_munchkins)
            {
               self->transmit_buffer[0] = 0xFF ^ self->buttons[0];
               self->transmit_buffer[1] = 0xFF ^ self->buttons[1];
               self->transmit_buffer[2] = self->axes[0][0];
               self->transmit_buffer[3] = self->axes[0][1];
               self->transmit_buffer[4] = self->axes[1][0];
               self->transmit_buffer[5] = self->axes[1][1];
               self->transmit_count = 6;
            }
            else
            {
               self->transmit_buffer[0] = 0xFF ^ self->buttons[0];
               self->transmit_buffer[1] = 0xFF ^ self->buttons[1];
               self->transmit_count = 2;

               if(!(self->rumble_magic[2] & 0xFE))
               {
                  self->transmit_buffer[self->transmit_count++] = 0x00;
                  self->transmit_buffer[self->transmit_count++] = 0x00;
               }
            }
            self->command_phase++;
            break;

         case 0x4201:			/*  Weak(in DS mode) */
            if(self->da_rumble_compat)
               self->rumble_param[0] = self->receive_buffer;
            /*  Dualshock weak */
            else if(self->rumble_magic[0] == 0x00 && self->rumble_magic[2] != 0x00 && self->rumble_magic[3] != 0x00 && self->rumble_magic[4] != 0x00 && self->rumble_magic[5] != 0x00)
               self->rumble_param[0] = self->receive_buffer;
            self->command_phase++;
            break;

         case 0x4202:
            if(self->da_rumble_compat)
               self->rumble_param[1] = self->receive_buffer;
            else if(self->rumble_magic[1] == 0x01)	/*  DualShock strong */
               self->rumble_param[1] = self->receive_buffer;
            else if(self->rumble_magic[1] == 0x00 && self->rumble_magic[2] != 0x00 && self->rumble_magic[3] != 0x00 && self->rumble_magic[4] != 0x00 && self->rumble_magic[5] != 0x00)	/*  DualShock weak */
               self->rumble_param[0] = self->receive_buffer;

            self->command_phase++;
            break;

         case 0x4203:
            if(self->da_rumble_compat)
            {

            }
            else if(self->rumble_magic[1] == 0x00 && self->rumble_magic[2] == 0x01)
               self->rumble_param[1] = self->receive_buffer;	/*  DualShock strong. */
            self->command_phase++;	/*  Nowhere here we come! */
            break;

            /************************/
            /* MMMode 1, Command 0x43 */
            /************************/
         case 0x4300:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0; /**/ self->transmit_pos = 0; self->transmit_count = 1; /**/
               self->command_phase++;
            }
            else
               self->command_phase = -1;
            break;

         case 0x4301:
            if(self->receive_buffer == 0x00)
            {
               /* fprintf(stderr, "Mad Munchkins mode left!\n"); */
               self->mad_munchkins = false;
            }
            self->transmit_buffer[0] = 0x00;
            self->transmit_buffer[1] = 0x00;
            self->transmit_buffer[2] = 0x00;
            self->transmit_buffer[3] = 0x00;
            self->transmit_buffer[4] = 0x00;
            self->transmit_pos = 0;
            self->transmit_count = 5;
            self->command_phase = -1;
            break;

            /************************/
            /* MMMode 1, Command 0x44 */
            /************************/
         case 0x4400:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0; /**/ self->transmit_pos = 0; self->transmit_count = 1; /**/
               self->command_phase++;
            }
            else
               self->command_phase = -1;
            break;

         case 0x4401:
            self->transmit_buffer[0] = 0x00;
            self->transmit_buffer[1] = 0x00;
            self->transmit_buffer[2] = 0x00;
            self->transmit_buffer[3] = 0x00;
            self->transmit_buffer[4] = 0x00;
            self->transmit_pos = 0;
            self->transmit_count = 5;
            self->command_phase++;

            /*  Ignores locking state. */
            switch(self->receive_buffer)
            {
               case 0x00:
                  self->analog_mode = false;
                  /* fprintf(stderr, "Analog mode disabled\n"); */
                  break;

               case 0x01:
                  self->analog_mode = true;
                  /* fprintf(stderr, "Analog mode enabled\n"); */
                  break;
            }
            break;

         case 0x4402:
            switch(self->receive_buffer)
            {
               case 0x02:
                  self->analog_mode_locked = false;
                  break;

               case 0x03:
                  self->analog_mode_locked = true;
                  break;
            }
            self->command_phase = -1;
            break;

            /************************/
            /* MMMode 1, Command 0x45 */
            /************************/
         case 0x4500:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0x01; /**/ self->transmit_pos = 0; self->transmit_count = 1; /**/
               self->command_phase++;
            }
            else
               self->command_phase = -1;
            break;

         case 0x4501:
            self->transmit_buffer[0] = 0x02;
            self->transmit_buffer[1] = self->analog_mode ? 0x01 : 0x00;
            self->transmit_buffer[2] = 0x02;
            self->transmit_buffer[3] = 0x01;
            self->transmit_buffer[4] = 0x00;
            self->transmit_pos = 0;
            self->transmit_count = 5;
            self->command_phase = -1;
            break;


            /************************/
            /* MMMode 1, Command 0x46 */
            /************************/
         case 0x4600:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0; /**/ self->transmit_pos = 0; self->transmit_count = 1; /**/
               self->command_phase++;
            }
            else
               self->command_phase = -1;
            break;

         case 0x4601:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0x00;
               self->transmit_buffer[1] = 0x01;
               self->transmit_buffer[2] = 0x02;
               self->transmit_buffer[3] = 0x00;
               self->transmit_buffer[4] = 0x0A;
            }
            else if(self->receive_buffer == 0x01)
            {
               self->transmit_buffer[0] = 0x00;
               self->transmit_buffer[1] = 0x01;
               self->transmit_buffer[2] = 0x01;
               self->transmit_buffer[3] = 0x01;
               self->transmit_buffer[4] = 0x14;
            }
            else
            {
               self->transmit_buffer[0] = 0x00;
               self->transmit_buffer[1] = 0x00;
               self->transmit_buffer[2] = 0x00;
               self->transmit_buffer[3] = 0x00;
               self->transmit_buffer[4] = 0x00;
            }
            self->transmit_pos = 0;
            self->transmit_count = 5;
            self->command_phase = -1;
            break;

            /************************/
            /* MMMode 1, Command 0x47 */
            /************************/
         case 0x4700:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0; /**/ self->transmit_pos = 0; self->transmit_count = 1; /**/
               self->command_phase++;
            }
            else
               self->command_phase = -1;
            break;

         case 0x4701:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0x00;
               self->transmit_buffer[1] = 0x02;
               self->transmit_buffer[2] = 0x00;
               self->transmit_buffer[3] = 0x01;
               self->transmit_buffer[4] = 0x00;
            }
            else
            {
               self->transmit_buffer[0] = 0x00;
               self->transmit_buffer[1] = 0x00;
               self->transmit_buffer[2] = 0x00;
               self->transmit_buffer[3] = 0x00;
               self->transmit_buffer[4] = 0x00;
            }
            self->transmit_pos = 0;
            self->transmit_count = 5;
            self->command_phase = -1;
            break;

            /************************/
            /* MMMode 1, Command 0x48 */
            /************************/
         case 0x4800:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0; /**/ self->transmit_pos = 0; self->transmit_count = 1; /**/
               self->command_phase++;
            }
            else
               self->command_phase = -1;
            break;

         case 0x4801:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0x00;
               self->transmit_buffer[1] = 0x00;
               self->transmit_buffer[2] = 0x00;
               self->transmit_buffer[3] = 0x01;
               self->transmit_buffer[4] = self->rumble_param[0];
            }
            else if(self->receive_buffer == 0x01)
            {
               self->transmit_buffer[0] = 0x00;
               self->transmit_buffer[1] = 0x00;
               self->transmit_buffer[2] = 0x00;
               self->transmit_buffer[3] = 0x01;
               self->transmit_buffer[4] = self->rumble_param[1];
            }
            else
            {
               self->transmit_buffer[0] = 0x00;
               self->transmit_buffer[1] = 0x00;
               self->transmit_buffer[2] = 0x00;
               self->transmit_buffer[3] = 0x00;
               self->transmit_buffer[4] = 0x00;
            }
            self->transmit_pos = 0;
            self->transmit_count = 5;
            self->command_phase = -1;
            break;

            /************************/
            /* MMMode 1, Command 0x49 */
            /************************/
         case 0x4900:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0; /**/ self->transmit_pos = 0; self->transmit_count = 1; /**/
               self->command_phase++;
            }
            else
               self->command_phase = -1;
            break;

         case 0x4901:
            self->transmit_buffer[0] = 0x00;
            self->transmit_buffer[1] = 0x00;
            self->transmit_buffer[2] = 0x00;
            self->transmit_buffer[3] = 0x00;
            self->transmit_buffer[4] = 0x00;
            self->transmit_pos = 0;
            self->transmit_count = 5;
            self->command_phase = -1;
            break;

            /************************/
            /* MMMode 1, Command 0x4A */
            /************************/
         case 0x4A00:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0; /**/ self->transmit_pos = 0; self->transmit_count = 1; /**/
               self->command_phase++;
            }
            else
               self->command_phase = -1;
            break;

         case 0x4A01:
            self->transmit_buffer[0] = 0x00;
            self->transmit_buffer[1] = 0x00;
            self->transmit_buffer[2] = 0x00;
            self->transmit_buffer[3] = 0x00;
            self->transmit_buffer[4] = 0x00;
            self->transmit_pos = 0;
            self->transmit_count = 5;
            self->command_phase = -1;
            break;

            /************************/
            /* MMMode 1, Command 0x4B */
            /************************/
         case 0x4B00:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0; /**/ self->transmit_pos = 0; self->transmit_count = 1; /**/
               self->command_phase++;
            }
            else
               self->command_phase = -1;
            break;

         case 0x4B01:
            self->transmit_buffer[0] = 0x00;
            self->transmit_buffer[1] = 0x00;
            self->transmit_buffer[2] = 0x00;
            self->transmit_buffer[3] = 0x00;
            self->transmit_buffer[4] = 0x00;
            self->transmit_pos = 0;
            self->transmit_count = 5;
            self->command_phase = -1;
            break;

            /************************/
            /* MMMode 1, Command 0x4C */
            /************************/
         case 0x4C00:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0; /**/ self->transmit_pos = 0; self->transmit_count = 1; /**/
               self->command_phase++;
            }
            else
               self->command_phase = -1;
            break;

         case 0x4C01:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0x00;
               self->transmit_buffer[1] = 0x00;
               self->transmit_buffer[2] = 0x04;
               self->transmit_buffer[3] = 0x00;
               self->transmit_buffer[4] = 0x00;
            }
            else if(self->receive_buffer == 0x01)
            {
               self->transmit_buffer[0] = 0x00;
               self->transmit_buffer[1] = 0x00;
               self->transmit_buffer[2] = 0x07;
               self->transmit_buffer[3] = 0x00;
               self->transmit_buffer[4] = 0x00;
            }
            else
            {
               self->transmit_buffer[0] = 0x00;
               self->transmit_buffer[1] = 0x00;
               self->transmit_buffer[2] = 0x00;
               self->transmit_buffer[3] = 0x00;
               self->transmit_buffer[4] = 0x00;
            }

            self->transmit_pos = 0;
            self->transmit_count = 5;
            self->command_phase = -1;
            break;

            /************************/
            /* MMMode 1, Command 0x4D */
            /************************/
         case 0x4D00:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = self->rumble_magic[0]; /**/ self->transmit_pos = 0; self->transmit_count = 1; /**/
               self->command_phase++;
            }
            else
               self->command_phase = -1;
            break;

         case 0x4D01:
         case 0x4D02:
         case 0x4D03:
         case 0x4D04:
         case 0x4D05:
         case 0x4D06:
            {
               unsigned index = self->command_phase - 0x4D01;

               if(index < 5)
               {
                  self->transmit_buffer[0] = self->rumble_magic[1 + index];
                  self->transmit_pos = 0;
                  self->transmit_count = 1;
                  self->command_phase++;
               }
               else
                  self->command_phase = -1;

               self->rumble_magic[index] = self->receive_buffer;	 
            }
            break;

            /************************/
            /* MMMode 1, Command 0x4E */
            /************************/
         case 0x4E00:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0; /**/ self->transmit_pos = 0; self->transmit_count = 1; /**/
               self->command_phase++;
            }
            else
               self->command_phase = -1;
            break;

         case 0x4E01:
            self->transmit_buffer[0] = 0x00;
            self->transmit_buffer[1] = 0x00;
            self->transmit_buffer[2] = 0x00;
            self->transmit_buffer[3] = 0x00;
            self->transmit_buffer[4] = 0x00;
            self->transmit_pos = 0;
            self->transmit_count = 5;
            self->command_phase = -1;
            break;


            /************************/
            /* MMMode 1, Command 0x4F */
            /************************/
         case 0x4F00:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0; /**/ self->transmit_pos = 0; self->transmit_count = 1; /**/
               self->command_phase++;
            }
            else
               self->command_phase = -1;
            break;

         case 0x4F01:
            self->transmit_buffer[0] = 0x00;
            self->transmit_buffer[1] = 0x00;
            self->transmit_buffer[2] = 0x00;
            self->transmit_buffer[3] = 0x00;
            self->transmit_buffer[4] = 0x00;
            self->transmit_pos = 0;
            self->transmit_count = 5;
            self->command_phase = -1;
            break;
      }
   }

   if(!self->bitpos && self->transmit_count)
      *dsr_pulse_delay = 0x40; /* 0x100; */

   return(ret);
}




/* ===========================================================================
 *  Merged from mednafen/psx/input/mouse.cpp
 * =========================================================================== */

typedef struct InputDevice_Mouse InputDevice_Mouse;
struct InputDevice_Mouse
{
   InputDevice base;
         int32_t lastts;
         int32_t clear_timeout;
         bool dtr;
         uint8_t button;
         uint8_t button_post_mask;
         int32_t accum_xdelta;
         int32_t accum_ydelta;
         int32_t command_phase;
         uint32_t bitpos;
         uint8_t receive_buffer;
         uint8_t command;
         uint8_t transmit_buffer[5];
         uint32_t transmit_pos;
         uint32_t transmit_count;
};


static void InputDevice_Mouse_Ctor(InputDevice *self_)
{
   (void)self_;

   InputDevice_Mouse_Power(self_);
}



static void InputDevice_Mouse_Update(InputDevice *self_, const int32_t timestamp)
{
   InputDevice_Mouse *self = (InputDevice_Mouse *)self_;

   int32_t cycles = timestamp - self->lastts;

   self->clear_timeout += cycles;
   if(self->clear_timeout >= (33868800 / 4))
   {
      /* puts("Mouse timeout\n"); */
      self->clear_timeout = 0;
      self->accum_xdelta = 0;
      self->accum_ydelta = 0;
      self->button &= self->button_post_mask;
   }

   self->lastts = timestamp;
}

static void InputDevice_Mouse_ResetTS(InputDevice *self_)
{
   InputDevice_Mouse *self = (InputDevice_Mouse *)self_;

   self->lastts = 0;
}

static void InputDevice_Mouse_Power(InputDevice *self_)
{
   InputDevice_Mouse *self = (InputDevice_Mouse *)self_;

   self->lastts = 0;
   self->clear_timeout = 0;

   self->dtr = 0;

   self->button = 0;
   self->button_post_mask = 0;
   self->accum_xdelta = 0;
   self->accum_ydelta = 0;

   self->command_phase = 0;

   self->bitpos = 0;

   self->receive_buffer = 0;

   self->command = 0;

   memset(self->transmit_buffer, 0, sizeof(self->transmit_buffer));

   self->transmit_pos = 0;
   self->transmit_count = 0;
}

static int InputDevice_Mouse_StateAction(InputDevice *self_, StateMem* sm, int load, int data_only, const char* section_name)
{
   InputDevice_Mouse *self = (InputDevice_Mouse *)self_;

   SFORMAT StateRegs[] =
   {
      SFVAR(self->clear_timeout),

      SFVAR(self->dtr),

      SFVAR(self->button),
      SFVAR(self->button_post_mask),
      SFVAR(self->accum_xdelta),
      SFVAR(self->accum_ydelta),

      SFVAR(self->command_phase),
      SFVAR(self->bitpos),
      SFVAR(self->receive_buffer),

      SFVAR(self->command),

      SFARRAY(self->transmit_buffer, sizeof(self->transmit_buffer)),
      SFVAR(self->transmit_pos),
      SFVAR(self->transmit_count),

      SFEND
   };
   int ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, section_name);

   if(load)
   {
      if(((uint64_t)self->transmit_pos + self->transmit_count) > sizeof(self->transmit_buffer))
      {
         self->transmit_pos = 0;
         self->transmit_count = 0;
      }
   }

   return(ret);
}

static void InputDevice_Mouse_UpdateInput(InputDevice *self_, const void *data)
{
   InputDevice_Mouse *self = (InputDevice_Mouse *)self_;
   const uint8_t *_p = (const uint8_t *)data;
   uint32_t _dx, _dy;

#ifdef MSB_FIRST
   _dx = (uint32_t)_p[0] | ((uint32_t)_p[1] << 8) | ((uint32_t)_p[2] << 16) | ((uint32_t)_p[3] << 24);
   _dy = (uint32_t)_p[4] | ((uint32_t)_p[5] << 8) | ((uint32_t)_p[6] << 16) | ((uint32_t)_p[7] << 24);
#else
   memcpy(&_dx, _p + 0, 4);
   memcpy(&_dy, _p + 4, 4);
#endif
   self->accum_xdelta += (int32_t)_dx;
   self->accum_ydelta += (int32_t)_dy;

   if(self->accum_xdelta > 30 * 127) self->accum_xdelta = 30 * 127;
   if(self->accum_xdelta < 30 * -128) self->accum_xdelta = 30 * -128;

   if(self->accum_ydelta > 30 * 127) self->accum_ydelta = 30 * 127;
   if(self->accum_ydelta < 30 * -128) self->accum_ydelta = 30 * -128;

   self->button |= *((uint8_t *)data + 8);
   self->button_post_mask = *((uint8_t *)data + 8);

   /* printf("%d %d\n", accum_xdelta, accum_ydelta); */
}


static void InputDevice_Mouse_SetDTR(InputDevice *self_, bool new_dtr)
{
   InputDevice_Mouse *self = (InputDevice_Mouse *)self_;

   if(!self->dtr && new_dtr)
   {
      self->command_phase = 0;
      self->bitpos = 0;
      self->transmit_pos = 0;
      self->transmit_count = 0;
   }
   else if(self->dtr && !new_dtr)
   {
      /* if(bitpos || transmit_count) */
      /*  printf("[PAD] Abort communication!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"); */
   }

   self->dtr = new_dtr;
}

static bool InputDevice_Mouse_Clock(InputDevice *self_, bool TxD, int32_t *dsr_pulse_delay)
{
   InputDevice_Mouse *self = (InputDevice_Mouse *)self_;

   bool ret = 1;

   *dsr_pulse_delay = 0;

   if(!self->dtr)
      return(1);

   if(self->transmit_count)
      ret = (self->transmit_buffer[self->transmit_pos] >> self->bitpos) & 1;

   self->receive_buffer &= ~(1 << self->bitpos);
   self->receive_buffer |= TxD << self->bitpos;
   self->bitpos = (self->bitpos + 1) & 0x7;

   if(!self->bitpos)
   {
      /* printf("[PAD] Receive: %02x -- command_phase=%d\n", receive_buffer, command_phase); */

      if(self->transmit_count)
      {
         self->transmit_pos++;
         self->transmit_count--;
      }


      switch(self->command_phase)
      {
         case 0:
            if(self->receive_buffer != 0x01)
               self->command_phase = -1;
            else
            {
               self->transmit_buffer[0] = 0x12;
               self->transmit_pos = 0;
               self->transmit_count = 1;
               self->command_phase++;
            }
            break;

         case 1:
            self->command = self->receive_buffer;
            self->command_phase++;

            self->transmit_buffer[0] = 0x5A;

            if(self->command == 0x42)
            {
               int32_t xdelta = self->accum_xdelta;
               int32_t ydelta = self->accum_ydelta;

               if(xdelta < -128) xdelta = -128;
               if(xdelta > 127) xdelta = 127;

               if(ydelta < -128) ydelta = -128;
               if(ydelta > 127) ydelta = 127;

               self->transmit_buffer[1] = 0xFF;
               self->transmit_buffer[2] = 0xFC ^ (self->button << 2);
               self->transmit_buffer[3] = xdelta;
               self->transmit_buffer[4] = ydelta;

               self->accum_xdelta -= xdelta;
               self->accum_ydelta -= ydelta;

               self->button &= self->button_post_mask;

               self->transmit_pos = 0;
               self->transmit_count = 5;

               self->clear_timeout = 0;
            }
            else
            {
               self->command_phase = -1;
               self->transmit_pos = 0;
               self->transmit_count = 0;
            }
            break;

      }
   }

   if(!self->bitpos && self->transmit_count)
      *dsr_pulse_delay = 0x40; /* 0x100; */

   return(ret);
}




/* ===========================================================================
 *  Merged from mednafen/psx/input/negcon.cpp
 * =========================================================================== */

typedef struct InputDevice_neGcon InputDevice_neGcon;
struct InputDevice_neGcon
{
   InputDevice base;
         bool dtr;
         uint8_t buttons[2];
         uint8_t twist;
         uint8_t anabuttons[3];
         int32_t command_phase;
         uint32_t bitpos;
         uint8_t receive_buffer;
         uint8_t command;
         uint8_t transmit_buffer[8];
         uint32_t transmit_pos;
         uint32_t transmit_count;
};


static void InputDevice_neGcon_Ctor(InputDevice *self_)
{
   (void)self_;

   InputDevice_neGcon_Power(self_);
}



static void InputDevice_neGcon_Power(InputDevice *self_)
{
   InputDevice_neGcon *self = (InputDevice_neGcon *)self_;

   self->dtr = 0;

   self->buttons[0] = self->buttons[1] = 0;
   self->twist = 0;
   self->anabuttons[0] = 0;
   self->anabuttons[1] = 0;
   self->anabuttons[2] = 0;

   self->command_phase = 0;

   self->bitpos = 0;

   self->receive_buffer = 0;

   self->command = 0;

   memset(self->transmit_buffer, 0, sizeof(self->transmit_buffer));

   self->transmit_pos = 0;
   self->transmit_count = 0;
}

static void InputDevice_neGcon_UpdateInput(InputDevice *self_, const void *data)
{
   InputDevice_neGcon *self = (InputDevice_neGcon *)self_;

   uint8_t *d8 = (uint8_t *)data;
   uint32_t _v4, _v8, _v12, _v16, _v20;
#ifdef MSB_FIRST
   _v4  = (uint32_t)d8[ 4] | ((uint32_t)d8[ 5] << 8) | ((uint32_t)d8[ 6] << 16) | ((uint32_t)d8[ 7] << 24);
   _v8  = (uint32_t)d8[ 8] | ((uint32_t)d8[ 9] << 8) | ((uint32_t)d8[10] << 16) | ((uint32_t)d8[11] << 24);
   _v12 = (uint32_t)d8[12] | ((uint32_t)d8[13] << 8) | ((uint32_t)d8[14] << 16) | ((uint32_t)d8[15] << 24);
   _v16 = (uint32_t)d8[16] | ((uint32_t)d8[17] << 8) | ((uint32_t)d8[18] << 16) | ((uint32_t)d8[19] << 24);
   _v20 = (uint32_t)d8[20] | ((uint32_t)d8[21] << 8) | ((uint32_t)d8[22] << 16) | ((uint32_t)d8[23] << 24);
#else
   memcpy(&_v4,  d8 +  4, 4);
   memcpy(&_v8,  d8 +  8, 4);
   memcpy(&_v12, d8 + 12, 4);
   memcpy(&_v16, d8 + 16, 4);
   memcpy(&_v20, d8 + 20, 4);
#endif

   self->buttons[0] = d8[0];
   self->buttons[1] = d8[1];

   self->twist = ((32768 + _v4 - (((int32_t)_v8 * 32768 + 16383) / 32767)) * 255 + 32767) / 65535;

   self->anabuttons[0] = (_v12 * 255 + 16383) / 32767;
   self->anabuttons[1] = (_v16 * 255 + 16383) / 32767;
   self->anabuttons[2] = (_v20 * 255 + 16383) / 32767;

   /* printf("%02x %02x %02x %02x\n", twist, anabuttons[0], anabuttons[1], anabuttons[2]); */
}


static void InputDevice_neGcon_SetDTR(InputDevice *self_, bool new_dtr)
{
   InputDevice_neGcon *self = (InputDevice_neGcon *)self_;

   if(!self->dtr && new_dtr)
   {
      self->command_phase = 0;
      self->bitpos = 0;
      self->transmit_pos = 0;
      self->transmit_count = 0;
   }
   else if(self->dtr && !new_dtr)
   {
      /* if(bitpos || transmit_count) */
      /*  printf("[PAD] Abort communication!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"); */
   }

   self->dtr = new_dtr;
}

static bool InputDevice_neGcon_GetDSR(InputDevice *self_)
{
   InputDevice_neGcon *self = (InputDevice_neGcon *)self_;

   if(!self->dtr)
      return(0);

   if(!self->bitpos && self->transmit_count)
      return(1);

   return(0);
}

static bool InputDevice_neGcon_Clock(InputDevice *self_, bool TxD, int32_t *dsr_pulse_delay)
{
   InputDevice_neGcon *self = (InputDevice_neGcon *)self_;

   bool ret = 1;

   *dsr_pulse_delay = 0;

   if(!self->dtr)
      return(1);

   if(self->transmit_count)
      ret = (self->transmit_buffer[self->transmit_pos] >> self->bitpos) & 1;

   self->receive_buffer &= ~(1 << self->bitpos);
   self->receive_buffer |= TxD << self->bitpos;
   self->bitpos = (self->bitpos + 1) & 0x7;

   if(!self->bitpos)
   {
      /* printf("[PAD] Receive: %02x -- command_phase=%d\n", receive_buffer, command_phase); */

      if(self->transmit_count)
      {
         self->transmit_pos++;
         self->transmit_count--;
      }


      switch(self->command_phase)
      {
         case 0:
            if(self->receive_buffer != 0x01)
               self->command_phase = -1;
            else
            {
               self->transmit_buffer[0] = 0x23;
               self->transmit_pos = 0;
               self->transmit_count = 1;
               self->command_phase++;
               *dsr_pulse_delay = 256;
            }
            break;

         case 1:
            self->command = self->receive_buffer;
            self->command_phase++;

            self->transmit_buffer[0] = 0x5A;

            /* if(command != 0x42) */
            /*  fprintf(stderr, "Gamepad unhandled command: 0x%02x\n", command); */

            if(self->command == 0x42)
            {
               self->transmit_buffer[1] = 0xFF ^ self->buttons[0];
               self->transmit_buffer[2] = 0xFF ^ self->buttons[1];
               self->transmit_buffer[3] = self->twist;			/*  Twist, 0x00 through 0xFF, 0x80 center. */
               self->transmit_buffer[4] = self->anabuttons[0];		/*  Analog button I, 0x00 through 0xFF, 0x00 = no pressing, 0xFF = max. */
               self->transmit_buffer[5] = self->anabuttons[1];		/*  Analog button II, "" */
               self->transmit_buffer[6] = self->anabuttons[2];		/*  Left shoulder analog button, "" */
               self->transmit_pos = 0;
               self->transmit_count = 7;
               *dsr_pulse_delay = 256;
            }
            else
            {
               self->command_phase = -1;
               self->transmit_buffer[1] = 0;
               self->transmit_buffer[2] = 0;
               self->transmit_pos = 0;
               self->transmit_count = 0;
            }
            break;

         case 2:
            if(self->transmit_count > 0)
               *dsr_pulse_delay = 128;
            break;
      }
   }

   return(ret);
}




/* ===========================================================================
 *  Merged from mednafen/psx/input/negconrumble.cpp
 * =========================================================================== */

/*
   TODO:
	If we ever call Update() more than once per video frame(IE 50/60Hz), we'll need to add debounce logic to the analog mode button evaluation code.
*/

/* Notes:
     While The neGcon itself never had rumble motors, quite a handfull of neGcon supported games support sending haptic feedback while in neGcon mode.
     By implementing a modified DualShock controller profile that identifies as a neGcon, this behavior can be replicated.

     Analog toggling has been left in place because, while the controller is essentially useless in 'Digital' mode, some games that support rumble refuse
     to continue past a controller error screen when starting in 'Analog' mode, so the toggle allows one to bypass this screen.

   Original notes from dualshock.cpp:
     Both DA and DS style rumblings work in both analog and digital modes.

     Regarding getting Dual Shock style rumble working, Sony is evil and/or mean.  The owl tells me to burn Sony with boiling oil.

     To enable Dual Shock-style rumble, the game has to at least enter MAD MUNCHKINS MODE with command 0x43, and send the appropriate data(not the actual rumble type data per-se)
     with command 0x4D.

     DualAnalog-style rumble support seems to be borked until power loss if MAD MUNCHKINS MODE is even entered once...investigate further.

     Command 0x44 in MAD MUNCHKINS MODE can turn on/off analog mode(and the light with it).

     Command 0x42 in MAD MUNCHKINS MODE will return the analog mode style gamepad data, even when analog mode is off.  In combination with command 0x44, this could hypothetically
     be used for using the light in the gamepad as some kind of game mechanic).

     Dual Analog-style rumble notes(some of which may apply to DS too):
	Rumble appears to stop if you hold DTR active(TODO: for how long? instant?). (TODO: investigate if it's still stopped even if a memory card device number is sent.  It may be, since rumble may
							      cause excessive current draw in combination with memory card access)

	Rumble will work even if you interrupt the communication process after you've sent the rumble data(via command 0x42).
		Though if you interrupt it when you've only sent partial rumble data, dragons will eat you and I don't know(seems to have timing-dependent or random effects or something;
	        based on VERY ROUGH testing).
*/

typedef struct InputDevice_neGconRumble InputDevice_neGconRumble;
struct InputDevice_neGconRumble
{
   InputDevice base;
         bool cur_ana_button_state;
         bool prev_ana_button_state;
         int64_t combo_anatoggle_counter;
         bool da_rumble_compat;
         bool analog_mode;
         bool analog_mode_locked;
         bool mad_munchkins;
         uint8_t rumble_magic[6];
         uint8_t rumble_param[2];
         bool dtr;
         uint8_t buttons[2];
         uint8_t twist;
         uint8_t anabuttons[3];
         int32_t command_phase;
         uint32_t bitpos;
         uint8_t receive_buffer;
         uint8_t command;
         uint8_t transmit_buffer[8];
         uint32_t transmit_pos;
         uint32_t transmit_count;
         bool am_prev_info;
         bool aml_prev_info;
         char gp_name[64];
         int32_t lastts;
         bool amct_enabled;
};


static void InputDevice_neGconRumble_Ctor(InputDevice *self_, const char *name)
{
   InputDevice_neGconRumble *self = (InputDevice_neGconRumble *)self_;

   strlcpy(self->gp_name, name, sizeof(self->gp_name));
   InputDevice_neGconRumble_Power(self_);
   self->am_prev_info = self->analog_mode;
   self->aml_prev_info = self->analog_mode_locked;
   self->amct_enabled = false;
}



static void InputDevice_neGconRumble_Update(InputDevice *self_, const int32_t timestamp)
{
   InputDevice_neGconRumble *self = (InputDevice_neGconRumble *)self_;

   self->lastts = timestamp;
}

static void InputDevice_neGconRumble_ResetTS(InputDevice *self_)
{
   InputDevice_neGconRumble *self = (InputDevice_neGconRumble *)self_;

   /* printf("%lld\n", combo_anatoggle_counter); */
   if(self->combo_anatoggle_counter >= 0)
      self->combo_anatoggle_counter += self->lastts;
   self->lastts = 0;
}

static void InputDevice_neGconRumble_SetAMCT(InputDevice *self_, bool enabled)
{
   InputDevice_neGconRumble *self = (InputDevice_neGconRumble *)self_;

   bool amct_prev_info = self->amct_enabled;
   self->amct_enabled = enabled;
   self->analog_mode = true;

   if (amct_prev_info == self->analog_mode && amct_prev_info == self->amct_enabled)
      return;

   self->am_prev_info = self->analog_mode;
}

/*  */
/*  This simulates the behavior of the actual DualShock(analog toggle button evaluation is suspended while DTR is active). */
/*  Call in Update(), and whenever dtr goes inactive in the port access code. */
static void InputDevice_neGconRumble_CheckManualAnaModeChange(InputDevice *self_)
{
   InputDevice_neGconRumble *self = (InputDevice_neGconRumble *)self_;

   if(!self->dtr)
   {
      bool need_mode_toggle = false;

      if(self->buttons[0] == 0x01) /*  Map this to Retropad Select as most other buttons used by the analog toggle combo don't work */
      {
         if(self->combo_anatoggle_counter == -1)
            self->combo_anatoggle_counter = 0;
         else if(self->combo_anatoggle_counter >= (44100))
         {
            need_mode_toggle = true;
            self->combo_anatoggle_counter = -2;
         }
      }
      else
         self->combo_anatoggle_counter = -1;

      if(need_mode_toggle)
         self->analog_mode = !self->analog_mode;

      self->prev_ana_button_state = self->cur_ana_button_state; 	/*  Don't move this outside of the if(!dtr) block! */
   }
}

static void InputDevice_neGconRumble_Power(InputDevice *self_)
{
   InputDevice_neGconRumble *self = (InputDevice_neGconRumble *)self_;

   self->combo_anatoggle_counter = -2;
   self->lastts = 0;
   /*  */
   /*  */

   self->dtr = 0;

   self->buttons[0] = self->buttons[1] = 0;
   self->twist = 0;
   self->anabuttons[0] = 0;
   self->anabuttons[1] = 0;
   self->anabuttons[2] = 0;

   self->command_phase = 0;

   self->bitpos = 0;

   self->receive_buffer = 0;

   self->command = 0;

   memset(self->transmit_buffer, 0, sizeof(self->transmit_buffer));

   self->transmit_pos = 0;
   self->transmit_count = 0;

   self->analog_mode = true;
   self->analog_mode_locked = false;

   self->mad_munchkins = false;
   memset(self->rumble_magic, 0xFF, sizeof(self->rumble_magic));
   memset(self->rumble_param, 0, sizeof(self->rumble_param));

   self->da_rumble_compat = true;

   self->prev_ana_button_state = false;
}

static int InputDevice_neGconRumble_StateAction(InputDevice *self_, StateMem* sm, int load, int data_only, const char* section_name)
{
   InputDevice_neGconRumble *self = (InputDevice_neGconRumble *)self_;

   SFORMAT StateRegs[] =
   {
      SFVAR(self->cur_ana_button_state),
      SFVAR(self->prev_ana_button_state),
      SFVAR(self->combo_anatoggle_counter),

      SFVAR(self->da_rumble_compat),

      SFVAR(self->analog_mode),
      SFVAR(self->analog_mode_locked),

      SFVAR(self->mad_munchkins),
      SFARRAY(self->rumble_magic, sizeof(self->rumble_magic)),

      SFARRAY(self->rumble_param, sizeof(self->rumble_param)),

      SFVAR(self->dtr),

      SFARRAY(self->buttons, sizeof(self->buttons)),

      SFVAR(self->command_phase),
      SFVAR(self->bitpos),
      SFVAR(self->receive_buffer),

      SFVAR(self->command),

      SFARRAY(self->transmit_buffer, sizeof(self->transmit_buffer)),
      SFVAR(self->transmit_pos),
      SFVAR(self->transmit_count),

      SFEND
   };
   int ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, section_name);

   if(load)
   {
      if(((uint64_t)self->transmit_pos + self->transmit_count) > sizeof(self->transmit_buffer))
      {
         self->transmit_pos = 0;
         self->transmit_count = 0;
      }
   }

   return(ret);
}

static void InputDevice_neGconRumble_UpdateInput(InputDevice *self_, const void *data)
{
   InputDevice_neGconRumble *self = (InputDevice_neGconRumble *)self_;

   uint8_t *d8 = (uint8_t *)data;
   uint32_t _v4, _v8, _v12, _v16, _v20;
#ifdef MSB_FIRST
   _v4  = (uint32_t)d8[ 4] | ((uint32_t)d8[ 5] << 8) | ((uint32_t)d8[ 6] << 16) | ((uint32_t)d8[ 7] << 24);
   _v8  = (uint32_t)d8[ 8] | ((uint32_t)d8[ 9] << 8) | ((uint32_t)d8[10] << 16) | ((uint32_t)d8[11] << 24);
   _v12 = (uint32_t)d8[12] | ((uint32_t)d8[13] << 8) | ((uint32_t)d8[14] << 16) | ((uint32_t)d8[15] << 24);
   _v16 = (uint32_t)d8[16] | ((uint32_t)d8[17] << 8) | ((uint32_t)d8[18] << 16) | ((uint32_t)d8[19] << 24);
   _v20 = (uint32_t)d8[20] | ((uint32_t)d8[21] << 8) | ((uint32_t)d8[22] << 16) | ((uint32_t)d8[23] << 24);
#else
   memcpy(&_v4,  d8 +  4, 4);
   memcpy(&_v8,  d8 +  8, 4);
   memcpy(&_v12, d8 + 12, 4);
   memcpy(&_v16, d8 + 16, 4);
   memcpy(&_v20, d8 + 20, 4);
#endif

   self->buttons[0] = d8[0];
   self->buttons[1] = d8[1];
   self->cur_ana_button_state = d8[2] & 0x01;

   self->twist = ((32768 + _v4 - (((int32_t)_v8 * 32768 + 16383) / 32767)) * 255 + 32767) / 65535;

   self->anabuttons[0] = (_v12 * 255 + 16383) / 32767;
   self->anabuttons[1] = (_v16 * 255 + 16383) / 32767;
   self->anabuttons[2] = (_v20 * 255 + 16383) / 32767;

   /* printf("%02x %02x %02x %02x\n", twist, anabuttons[0], anabuttons[1], anabuttons[2]); */

   /* printf("RUMBLE: 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n", rumble_magic[0], rumble_magic[1], rumble_magic[2], rumble_magic[3], rumble_magic[4], rumble_magic[5]); */
   /* printf("%d, 0x%02x 0x%02x\n", da_rumble_compat, rumble_param[0], rumble_param[1]); */
   if(self->da_rumble_compat == false)
   {
      uint8_t sneaky_weaky = 0;
      uint32_t _rv;

      if(self->rumble_param[0] == 0x01)
         sneaky_weaky = 0xFF;

      /* revert to 0.9.33, should be fixed on libretro side instead */
      /* MDFN_en16lsb(rumb_dp, (sneaky_weaky << 0) | (rumble_param[1] << 8)); */

      _rv = (uint32_t)((sneaky_weaky << 0) | (self->rumble_param[1] << 8));
#ifdef MSB_FIRST
      d8[4 + 32 + 0] = _rv;
      d8[4 + 32 + 1] = _rv >> 8;
      d8[4 + 32 + 2] = _rv >> 16;
      d8[4 + 32 + 3] = _rv >> 24;
#else
      memcpy(&d8[4 + 32 + 0], &_rv, 4);
#endif
   }
   else
   {
      uint8_t sneaky_weaky = 0;
      uint32_t _rv;

      if(((self->rumble_param[0] & 0xC0) == 0x40) && ((self->rumble_param[1] & 0x01) == 0x01))
         sneaky_weaky = 0xFF;

      /* revert to 0.9.33, should be fixed on libretro side instead */
      /* MDFN_en16lsb(rumb_dp, sneaky_weaky << 0); */
      _rv = (uint32_t)(sneaky_weaky << 0);
#ifdef MSB_FIRST
      d8[4 + 32 + 0] = _rv;
      d8[4 + 32 + 1] = _rv >> 8;
      d8[4 + 32 + 2] = _rv >> 16;
      d8[4 + 32 + 3] = _rv >> 24;
#else
      memcpy(&d8[4 + 32 + 0], &_rv, 4);
#endif
   }

   InputDevice_neGconRumble_CheckManualAnaModeChange(self_);

   if(self->am_prev_info != self->analog_mode || self->aml_prev_info != self->analog_mode_locked)
      osd_message(2, RETRO_LOG_INFO,
            RETRO_MESSAGE_TARGET_OSD, RETRO_MESSAGE_TYPE_NOTIFICATION_ALT,
            "%s: neGcon mode is %s",
            self->gp_name, self->analog_mode ? "ON" : "OFF");

   self->aml_prev_info = self->analog_mode_locked;
   self->am_prev_info = self->analog_mode;
}


static void InputDevice_neGconRumble_SetDTR(InputDevice *self_, bool new_dtr)
{
   InputDevice_neGconRumble *self = (InputDevice_neGconRumble *)self_;

   const bool old_dtr = self->dtr;
   self->dtr = new_dtr;	/*  Set it to new state before we call InputDevice_neGconRumble_CheckManualAnaModeChange(self_). */

   if(!old_dtr && self->dtr)
   {
      self->command_phase = 0;
      self->bitpos = 0;
      self->transmit_pos = 0;
      self->transmit_count = 0;
   }
   else if(old_dtr && !self->dtr)
   {
      InputDevice_neGconRumble_CheckManualAnaModeChange(self_);
      /* if(bitpos || transmit_count) */
      /*  printf("[PAD] Abort communication!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"); */
   }
}

static bool InputDevice_neGconRumble_GetDSR(InputDevice *self_)
{
   InputDevice_neGconRumble *self = (InputDevice_neGconRumble *)self_;

   if(!self->dtr)
      return(0);

   if(!self->bitpos && self->transmit_count)
      return(1);

   return(0);
}

static bool InputDevice_neGconRumble_Clock(InputDevice *self_, bool TxD, int32_t *dsr_pulse_delay)
{
   InputDevice_neGconRumble *self = (InputDevice_neGconRumble *)self_;

   bool ret = 1;

   *dsr_pulse_delay = 0;

   if(!self->dtr)
      return(1);

   if(self->transmit_count)
      ret = (self->transmit_buffer[self->transmit_pos] >> self->bitpos) & 1;

   self->receive_buffer &= ~(1 << self->bitpos);
   self->receive_buffer |= TxD << self->bitpos;
   self->bitpos = (self->bitpos + 1) & 0x7;

   if(!self->bitpos)
   {
      /* if(command == 0x44) */
      /* if(command == 0x4D) //mad_munchkins) // || command == 0x43) */
      /*  fprintf(stderr, "[PAD] Receive: %02x -- command=%02x, command_phase=%d, transmit_pos=%d\n", receive_buffer, command, command_phase, transmit_pos); */

      if(self->transmit_count)
      {
         self->transmit_pos++;
         self->transmit_count--;
      }

      switch(self->command_phase)
      {
         case 0:
            if(self->receive_buffer != 0x01)
               self->command_phase = -1;
            else
            {
               if(self->mad_munchkins)
               {
                  self->transmit_buffer[0] = 0xF3;
                  self->transmit_pos = 0;
                  self->transmit_count = 1;
                  self->command_phase = 101;
               }
               else
               {
                  self->transmit_buffer[0] = self->analog_mode ? 0x23 : 0x41;
                  self->transmit_pos = 0;
                  self->transmit_count = 1;
                  self->command_phase++;
               }
            }
            break;

         case 1:
            self->command = self->receive_buffer;
            self->command_phase++;

            self->transmit_buffer[0] = 0x5A;

            /* fprintf(stderr, "Gamepad command: 0x%02x\n", command); */
            /* if(command != 0x42 && command != 0x43) */
            /*  fprintf(stderr, "Gamepad unhandled command: 0x%02x\n", command); */

            if(self->command == 0x42)
            {
               self->transmit_buffer[0] = 0x5A;
               self->transmit_pos = 0;
               self->transmit_count = 1;
               self->command_phase = (self->command << 8) | 0x00;
            }
            else if(self->command == 0x43)
            {
               self->transmit_pos = 0;
               if(self->analog_mode)
               {
                  self->transmit_buffer[1] = 0xFF ^ self->buttons[0];
                  self->transmit_buffer[2] = 0xFF ^ self->buttons[1];
                  self->transmit_buffer[3] = self->twist;			/*  Twist, 0x00 through 0xFF, 0x80 center. */
                  self->transmit_buffer[4] = self->anabuttons[0];		/*  Analog button I, 0x00 through 0xFF, 0x00 = no pressing, 0xFF = max. */
                  self->transmit_buffer[5] = self->anabuttons[1];		/*  Analog button II, "" */
                  self->transmit_buffer[6] = self->anabuttons[2];		/*  Left shoulder analog button, "" */
                  self->transmit_count = 7;
               }
               else
               {
                  self->transmit_buffer[1] = 0xFF ^ self->buttons[0];
                  self->transmit_buffer[2] = 0xFF ^ self->buttons[1];
                  self->transmit_count = 3;
               }
            }
            else
            {
               self->command_phase = -1;
               self->transmit_buffer[1] = 0;
               self->transmit_buffer[2] = 0;
               self->transmit_pos = 0;
               self->transmit_count = 0;
            }
            break;

         case 2:
            {
               if(self->command == 0x43 && self->transmit_pos == 2 && (self->receive_buffer == 0x01))
               {
                  /* fprintf(stderr, "Mad Munchkins mode entered!\n"); */
                  self->mad_munchkins = true;

                  if(self->da_rumble_compat)
                  {
                     self->rumble_param[0] = 0;
                     self->rumble_param[1] = 0;
                     self->da_rumble_compat = false;
                  }
                  self->command_phase = -1;
               }
            }
            break;

         case 101:
            self->command = self->receive_buffer;

            /* fprintf(stderr, "Mad Munchkins DualShock command: 0x%02x\n", command); */

            if(self->command >= 0x40 && self->command <= 0x4F)
            {
               self->transmit_buffer[0] = 0x5A;
               self->transmit_pos = 0;
               self->transmit_count = 1;
               self->command_phase = (self->command << 8) | 0x00;
            }
            else
            {
               self->transmit_count = 0;
               self->command_phase = -1;
            }
            break;

            /************************/
            /* MMMode 1, Command 0x40 */
            /************************/
         case 0x4000:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0; /**/ self->transmit_pos = 0; self->transmit_count = 1; /**/
               self->command_phase++;
            }
            else
               self->command_phase = -1;
            break;

         case 0x4001:
            self->transmit_buffer[0] = 0x00;
            self->transmit_buffer[1] = 0x00;
            self->transmit_buffer[2] = 0x00;
            self->transmit_buffer[3] = 0x00;
            self->transmit_buffer[4] = 0x00;
            self->transmit_pos = 0;
            self->transmit_count = 5;
            self->command_phase = -1;
            break;


            /************************/
            /* MMMode 1, Command 0x41 */
            /************************/
         case 0x4100:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0; /**/ self->transmit_pos = 0; self->transmit_count = 1; /**/
               self->command_phase++;
            }
            else
               self->command_phase = -1;
            break;

         case 0x4101:
            self->transmit_buffer[0] = 0x00;
            self->transmit_buffer[1] = 0x00;
            self->transmit_buffer[2] = 0x00;
            self->transmit_buffer[3] = 0x00;
            self->transmit_buffer[4] = 0x00;
            self->transmit_pos = 0;
            self->transmit_count = 5;
            self->command_phase = -1;
            break;

            /**************************/
            /* MMMode 0&1, Command 0x42 */
            /**************************/
         case 0x4200:
            self->transmit_pos = 0;
            if(self->analog_mode || self->mad_munchkins)
            {
               self->transmit_buffer[0] = 0xFF ^ self->buttons[0];
               self->transmit_buffer[1] = 0xFF ^ self->buttons[1];
               self->transmit_buffer[2] = self->twist;			/*  Twist, 0x00 through 0xFF, 0x80 center. */
               self->transmit_buffer[3] = self->anabuttons[0];		/*  Analog button I, 0x00 through 0xFF, 0x00 = no pressing, 0xFF = max. */
               self->transmit_buffer[4] = self->anabuttons[1];		/*  Analog button II, "" */
               self->transmit_buffer[5] = self->anabuttons[2];		/*  Left shoulder analog button, "" */
               self->transmit_count = 6;
            }
            else
            {
               self->transmit_buffer[0] = 0xFF ^ self->buttons[0];
               self->transmit_buffer[1] = 0xFF ^ self->buttons[1];
               self->transmit_count = 2;

               if(!(self->rumble_magic[2] & 0xFE))
               {
                  self->transmit_buffer[self->transmit_count++] = 0x00;
                  self->transmit_buffer[self->transmit_count++] = 0x00;
               }
            }
            self->command_phase++;
            break;

         case 0x4201:			/*  Weak(in DS mode) */
            if(self->da_rumble_compat)
               self->rumble_param[0] = self->receive_buffer;
            /*  Dualshock weak */
            else if(self->rumble_magic[0] == 0x00 && self->rumble_magic[2] != 0x00 && self->rumble_magic[3] != 0x00 && self->rumble_magic[4] != 0x00 && self->rumble_magic[5] != 0x00)
               self->rumble_param[0] = self->receive_buffer;
            self->command_phase++;
            break;

         case 0x4202:
            if(self->da_rumble_compat)
               self->rumble_param[1] = self->receive_buffer;
            else if(self->rumble_magic[1] == 0x01)	/*  DualShock strong */
               self->rumble_param[1] = self->receive_buffer;
            else if(self->rumble_magic[1] == 0x00 && self->rumble_magic[2] != 0x00 && self->rumble_magic[3] != 0x00 && self->rumble_magic[4] != 0x00 && self->rumble_magic[5] != 0x00)	/*  DualShock weak */
               self->rumble_param[0] = self->receive_buffer;

            self->command_phase++;
            break;

         case 0x4203:
            if(self->da_rumble_compat)
            {

            }
            else if(self->rumble_magic[1] == 0x00 && self->rumble_magic[2] == 0x01)
               self->rumble_param[1] = self->receive_buffer;	/*  DualShock strong. */
            self->command_phase++;	/*  Nowhere here we come! */
            break;

            /************************/
            /* MMMode 1, Command 0x43 */
            /************************/
         case 0x4300:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0; /**/ self->transmit_pos = 0; self->transmit_count = 1; /**/
               self->command_phase++;
            }
            else
               self->command_phase = -1;
            break;

         case 0x4301:
            if(self->receive_buffer == 0x00)
            {
               /* fprintf(stderr, "Mad Munchkins mode left!\n"); */
               self->mad_munchkins = false;
            }
            self->transmit_buffer[0] = 0x00;
            self->transmit_buffer[1] = 0x00;
            self->transmit_buffer[2] = 0x00;
            self->transmit_buffer[3] = 0x00;
            self->transmit_buffer[4] = 0x00;
            self->transmit_pos = 0;
            self->transmit_count = 5;
            self->command_phase = -1;
            break;

            /************************/
            /* MMMode 1, Command 0x44 */
            /************************/
         case 0x4400:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0; /**/ self->transmit_pos = 0; self->transmit_count = 1; /**/
               self->command_phase++;
            }
            else
               self->command_phase = -1;
            break;

         case 0x4401:
            self->transmit_buffer[0] = 0x00;
            self->transmit_buffer[1] = 0x00;
            self->transmit_buffer[2] = 0x00;
            self->transmit_buffer[3] = 0x00;
            self->transmit_buffer[4] = 0x00;
            self->transmit_pos = 0;
            self->transmit_count = 5;
            self->command_phase++;

            /*  Ignores locking state. */
            switch(self->receive_buffer)
            {
               case 0x00:
                  self->analog_mode = true;
                  /* fprintf(stderr, "Analog mode disabled\n"); */
                  break;

               case 0x01:
                  self->analog_mode = true;
                  /* fprintf(stderr, "Analog mode enabled\n"); */
                  break;
            }
            break;

         case 0x4402:
            switch(self->receive_buffer)
            {
               case 0x02:
                  self->analog_mode_locked = true;
                  break;

               case 0x03:
                  self->analog_mode_locked = true;
                  break;
            }
            self->command_phase = -1;
            break;

            /************************/
            /* MMMode 1, Command 0x45 */
            /************************/
         case 0x4500:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0x01; /**/ self->transmit_pos = 0; self->transmit_count = 1; /**/
               self->command_phase++;
            }
            else
               self->command_phase = -1;
            break;

         case 0x4501:
            self->transmit_buffer[0] = 0x02;
            self->transmit_buffer[1] = self->analog_mode ? 0x01 : 0x00;
            self->transmit_buffer[2] = 0x02;
            self->transmit_buffer[3] = 0x01;
            self->transmit_buffer[4] = 0x00;
            self->transmit_pos = 0;
            self->transmit_count = 5;
            self->command_phase = -1;
            break;


            /************************/
            /* MMMode 1, Command 0x46 */
            /************************/
         case 0x4600:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0; /**/ self->transmit_pos = 0; self->transmit_count = 1; /**/
               self->command_phase++;
            }
            else
               self->command_phase = -1;
            break;

         case 0x4601:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0x00;
               self->transmit_buffer[1] = 0x01;
               self->transmit_buffer[2] = 0x02;
               self->transmit_buffer[3] = 0x00;
               self->transmit_buffer[4] = 0x0A;
            }
            else if(self->receive_buffer == 0x01)
            {
               self->transmit_buffer[0] = 0x00;
               self->transmit_buffer[1] = 0x01;
               self->transmit_buffer[2] = 0x01;
               self->transmit_buffer[3] = 0x01;
               self->transmit_buffer[4] = 0x14;
            }
            else
            {
               self->transmit_buffer[0] = 0x00;
               self->transmit_buffer[1] = 0x00;
               self->transmit_buffer[2] = 0x00;
               self->transmit_buffer[3] = 0x00;
               self->transmit_buffer[4] = 0x00;
            }
            self->transmit_pos = 0;
            self->transmit_count = 5;
            self->command_phase = -1;
            break;

            /************************/
            /* MMMode 1, Command 0x47 */
            /************************/
         case 0x4700:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0; /**/ self->transmit_pos = 0; self->transmit_count = 1; /**/
               self->command_phase++;
            }
            else
               self->command_phase = -1;
            break;

         case 0x4701:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0x00;
               self->transmit_buffer[1] = 0x02;
               self->transmit_buffer[2] = 0x00;
               self->transmit_buffer[3] = 0x01;
               self->transmit_buffer[4] = 0x00;
            }
            else
            {
               self->transmit_buffer[0] = 0x00;
               self->transmit_buffer[1] = 0x00;
               self->transmit_buffer[2] = 0x00;
               self->transmit_buffer[3] = 0x00;
               self->transmit_buffer[4] = 0x00;
            }
            self->transmit_pos = 0;
            self->transmit_count = 5;
            self->command_phase = -1;
            break;

            /************************/
            /* MMMode 1, Command 0x48 */
            /************************/
         case 0x4800:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0; /**/ self->transmit_pos = 0; self->transmit_count = 1; /**/
               self->command_phase++;
            }
            else
               self->command_phase = -1;
            break;

         case 0x4801:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0x00;
               self->transmit_buffer[1] = 0x00;
               self->transmit_buffer[2] = 0x00;
               self->transmit_buffer[3] = 0x01;
               self->transmit_buffer[4] = self->rumble_param[0];
            }
            else if(self->receive_buffer == 0x01)
            {
               self->transmit_buffer[0] = 0x00;
               self->transmit_buffer[1] = 0x00;
               self->transmit_buffer[2] = 0x00;
               self->transmit_buffer[3] = 0x01;
               self->transmit_buffer[4] = self->rumble_param[1];
            }
            else
            {
               self->transmit_buffer[0] = 0x00;
               self->transmit_buffer[1] = 0x00;
               self->transmit_buffer[2] = 0x00;
               self->transmit_buffer[3] = 0x00;
               self->transmit_buffer[4] = 0x00;
            }
            self->transmit_pos = 0;
            self->transmit_count = 5;
            self->command_phase = -1;
            break;

            /************************/
            /* MMMode 1, Command 0x49 */
            /************************/
         case 0x4900:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0; /**/ self->transmit_pos = 0; self->transmit_count = 1; /**/
               self->command_phase++;
            }
            else
               self->command_phase = -1;
            break;

         case 0x4901:
            self->transmit_buffer[0] = 0x00;
            self->transmit_buffer[1] = 0x00;
            self->transmit_buffer[2] = 0x00;
            self->transmit_buffer[3] = 0x00;
            self->transmit_buffer[4] = 0x00;
            self->transmit_pos = 0;
            self->transmit_count = 5;
            self->command_phase = -1;
            break;

            /************************/
            /* MMMode 1, Command 0x4A */
            /************************/
         case 0x4A00:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0; /**/ self->transmit_pos = 0; self->transmit_count = 1; /**/
               self->command_phase++;
            }
            else
               self->command_phase = -1;
            break;

         case 0x4A01:
            self->transmit_buffer[0] = 0x00;
            self->transmit_buffer[1] = 0x00;
            self->transmit_buffer[2] = 0x00;
            self->transmit_buffer[3] = 0x00;
            self->transmit_buffer[4] = 0x00;
            self->transmit_pos = 0;
            self->transmit_count = 5;
            self->command_phase = -1;
            break;

            /************************/
            /* MMMode 1, Command 0x4B */
            /************************/
         case 0x4B00:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0; /**/ self->transmit_pos = 0; self->transmit_count = 1; /**/
               self->command_phase++;
            }
            else
               self->command_phase = -1;
            break;

         case 0x4B01:
            self->transmit_buffer[0] = 0x00;
            self->transmit_buffer[1] = 0x00;
            self->transmit_buffer[2] = 0x00;
            self->transmit_buffer[3] = 0x00;
            self->transmit_buffer[4] = 0x00;
            self->transmit_pos = 0;
            self->transmit_count = 5;
            self->command_phase = -1;
            break;

            /************************/
            /* MMMode 1, Command 0x4C */
            /************************/
         case 0x4C00:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0; /**/ self->transmit_pos = 0; self->transmit_count = 1; /**/
               self->command_phase++;
            }
            else
               self->command_phase = -1;
            break;

         case 0x4C01:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0x00;
               self->transmit_buffer[1] = 0x00;
               self->transmit_buffer[2] = 0x04;
               self->transmit_buffer[3] = 0x00;
               self->transmit_buffer[4] = 0x00;
            }
            else if(self->receive_buffer == 0x01)
            {
               self->transmit_buffer[0] = 0x00;
               self->transmit_buffer[1] = 0x00;
               self->transmit_buffer[2] = 0x07;
               self->transmit_buffer[3] = 0x00;
               self->transmit_buffer[4] = 0x00;
            }
            else
            {
               self->transmit_buffer[0] = 0x00;
               self->transmit_buffer[1] = 0x00;
               self->transmit_buffer[2] = 0x00;
               self->transmit_buffer[3] = 0x00;
               self->transmit_buffer[4] = 0x00;
            }

            self->transmit_pos = 0;
            self->transmit_count = 5;
            self->command_phase = -1;
            break;

            /************************/
            /* MMMode 1, Command 0x4D */
            /************************/
         case 0x4D00:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = self->rumble_magic[0]; /**/ self->transmit_pos = 0; self->transmit_count = 1; /**/
               self->command_phase++;
            }
            else
               self->command_phase = -1;
            break;

         case 0x4D01:
         case 0x4D02:
         case 0x4D03:
         case 0x4D04:
         case 0x4D05:
         case 0x4D06:
            {
               unsigned index = self->command_phase - 0x4D01;

               if(index < 5)
               {
                  self->transmit_buffer[0] = self->rumble_magic[1 + index];
                  self->transmit_pos = 0;
                  self->transmit_count = 1;
                  self->command_phase++;
               }
               else
                  self->command_phase = -1;

               self->rumble_magic[index] = self->receive_buffer;	 
            }
            break;

            /************************/
            /* MMMode 1, Command 0x4E */
            /************************/
         case 0x4E00:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0; /**/ self->transmit_pos = 0; self->transmit_count = 1; /**/
               self->command_phase++;
            }
            else
               self->command_phase = -1;
            break;

         case 0x4E01:
            self->transmit_buffer[0] = 0x00;
            self->transmit_buffer[1] = 0x00;
            self->transmit_buffer[2] = 0x00;
            self->transmit_buffer[3] = 0x00;
            self->transmit_buffer[4] = 0x00;
            self->transmit_pos = 0;
            self->transmit_count = 5;
            self->command_phase = -1;
            break;


            /************************/
            /* MMMode 1, Command 0x4F */
            /************************/
         case 0x4F00:
            if(self->receive_buffer == 0x00)
            {
               self->transmit_buffer[0] = 0; /**/ self->transmit_pos = 0; self->transmit_count = 1; /**/
               self->command_phase++;
            }
            else
               self->command_phase = -1;
            break;

         case 0x4F01:
            self->transmit_buffer[0] = 0x00;
            self->transmit_buffer[1] = 0x00;
            self->transmit_buffer[2] = 0x00;
            self->transmit_buffer[3] = 0x00;
            self->transmit_buffer[4] = 0x00;
            self->transmit_pos = 0;
            self->transmit_count = 5;
            self->command_phase = -1;
            break;
      }
   }

   if(!self->bitpos && self->transmit_count)
      *dsr_pulse_delay = 0x40; /* 0x100; */

   return(ret);
}




/* ===========================================================================
 *  Merged from mednafen/psx/input/guncon.cpp
 * =========================================================================== */

typedef struct InputDevice_GunCon InputDevice_GunCon;
struct InputDevice_GunCon
{
   InputDevice base;
         bool dtr;
         uint8_t buttons;
         bool trigger_eff;
         bool trigger_noclear;
         uint16_t hit_x, hit_y;
         int16_t nom_x, nom_y;
         int32_t os_shot_counter;
         bool prev_oss;
         int32_t command_phase;
         uint32_t bitpos;
         uint8_t receive_buffer;
         uint8_t command;
         uint8_t transmit_buffer[16];
         uint32_t transmit_pos;
         uint32_t transmit_count;
         bool prev_vsync;
         int line_counter;
};


static void InputDevice_GunCon_Ctor(InputDevice *self_)
{
   (void)self_;

   InputDevice_GunCon_Power(self_);
}



static void InputDevice_GunCon_Power(InputDevice *self_)
{
   InputDevice_GunCon *self = (InputDevice_GunCon *)self_;

   self->dtr = 0;

   self->buttons = 0;
   self->trigger_eff = 0;
   self->trigger_noclear = 0;
   self->hit_x = 0;
   self->hit_y = 0;

   self->nom_x = 0;
   self->nom_y = 0;

   self->os_shot_counter = 0;
   self->prev_oss = 0;

   self->command_phase = 0;

   self->bitpos = 0;

   self->receive_buffer = 0;

   self->command = 0;

   memset(self->transmit_buffer, 0, sizeof(self->transmit_buffer));

   self->transmit_pos = 0;
   self->transmit_count = 0;

   self->prev_vsync = 0;
   self->line_counter = 0;
}

static int InputDevice_GunCon_StateAction(InputDevice *self_, StateMem* sm, int load, int data_only, const char* section_name)
{
   InputDevice_GunCon *self = (InputDevice_GunCon *)self_;

   SFORMAT StateRegs[] =
   {
      SFVAR(self->dtr),

      SFVAR(self->buttons),
      SFVAR(self->trigger_eff),
      SFVAR(self->trigger_noclear),
      SFVAR(self->hit_x),
      SFVAR(self->hit_y),

      SFVAR(self->nom_x),
      SFVAR(self->nom_y),
      SFVAR(self->os_shot_counter),
      SFVAR(self->prev_oss),

      SFVAR(self->command_phase),
      SFVAR(self->bitpos),
      SFVAR(self->receive_buffer),

      SFVAR(self->command),

      SFARRAY(self->transmit_buffer, sizeof(self->transmit_buffer)),
      SFVAR(self->transmit_pos),
      SFVAR(self->transmit_count),

      SFVAR(self->prev_vsync),
      SFVAR(self->line_counter),

      SFEND
   };
   int ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, section_name);

   if(load)
   {
      if(((uint64_t)self->transmit_pos + self->transmit_count) > sizeof(self->transmit_buffer))
      {
         self->transmit_pos = 0;
         self->transmit_count = 0;
      }
   }

   return(ret);
}

static void InputDevice_GunCon_UpdateInput(InputDevice *self_, const void *data)
{
   InputDevice_GunCon *self = (InputDevice_GunCon *)self_;

   uint8_t *d8 = (uint8_t *)data;
   {
      uint16_t _x, _y;
#ifdef MSB_FIRST
      _x = (uint16_t)d8[0] | ((uint16_t)d8[1] << 8);
      _y = (uint16_t)d8[2] | ((uint16_t)d8[3] << 8);
#else
      memcpy(&_x, &d8[0], 2);
      memcpy(&_y, &d8[2], 2);
#endif
      self->nom_x = (int16_t)_x;
      self->nom_y = (int16_t)_y;
   }

   self->trigger_noclear = (bool)(d8[4] & 0x1);
   self->trigger_eff |= self->trigger_noclear;

   self->buttons = d8[4] >> 1;

   if(self->os_shot_counter > 0)	/*  FIXME if UpdateInput() is ever called more than once per video frame(at ~50 or ~60Hz). */
      self->os_shot_counter--;

   if((d8[4] & 0x8) && !self->prev_oss && self->os_shot_counter == 0)
      self->os_shot_counter = 4;
   self->prev_oss = d8[4] & 0x8;
}

static int32_t InputDevice_GunCon_GPULineHook(InputDevice *self_, const int32_t line_timestamp, bool vsync, uint32_t *pixels, const unsigned width,
      const unsigned pix_clock_offset, const unsigned pix_clock, const unsigned pix_clock_divider, const unsigned surf_pitchinpix, const unsigned upscale_factor)
{
   InputDevice_GunCon *self = (InputDevice_GunCon *)self_;

   if(vsync && !self->prev_vsync)
      self->line_counter = 0;

   if(pixels && pix_clock)
   {
      const int avs = 16; /*  Not 16 for PAL, fixme. */
      int32_t gx;
      int32_t gy;
      int32_t ix;

      gx = (self->nom_x * 2 + pix_clock_divider) / (pix_clock_divider * 2);
      gy = self->nom_y;

      for (ix = gx; ix < (gx + (int32_t)(pix_clock / 762925)); ix++)
      {
         if(ix >= 0 && ix < (int)width && self->line_counter >= (avs + gy) && self->line_counter < (avs + gy + 8))
         {
            int r, g, b, a;

            MDFN_DecodeColor(pixels[ix * upscale_factor], &r, &g, &b, &a);

            if((r + g + b) >= 0x40)	/*  Wrong, but not COMPLETELY ABSOLUTELY wrong, at least. ;) */
            {
               self->hit_x = (int64_t)(ix + pix_clock_offset) * 8000000 / pix_clock;	/*  GunCon has what appears to be an 8.00MHz ceramic resonator in it. */
               self->hit_y = self->line_counter;
            }
         }
      }

      self->base.chair_x = gx;
      self->base.chair_y = (avs + gy) - self->line_counter;
   }

   self->line_counter++;

   return(PSX_EVENT_MAXTS);
}

static void InputDevice_GunCon_SetDTR(InputDevice *self_, bool new_dtr)
{
   InputDevice_GunCon *self = (InputDevice_GunCon *)self_;

   if(!self->dtr && new_dtr)
   {
      self->command_phase = 0;
      self->bitpos = 0;
      self->transmit_pos = 0;
      self->transmit_count = 0;
   }
   else if(self->dtr && !new_dtr)
   {
      /* if(bitpos || transmit_count) */
      /*  printf("[PAD] Abort communication!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"); */
   }

   self->dtr = new_dtr;
}

static bool InputDevice_GunCon_GetDSR(InputDevice *self_)
{
   InputDevice_GunCon *self = (InputDevice_GunCon *)self_;

   if(!self->dtr)
      return(0);

   if(!self->bitpos && self->transmit_count)
      return(1);

   return(0);
}

static bool InputDevice_GunCon_Clock(InputDevice *self_, bool TxD, int32_t *dsr_pulse_delay)
{
   InputDevice_GunCon *self = (InputDevice_GunCon *)self_;

   bool ret = 1;

   *dsr_pulse_delay = 0;

   if(!self->dtr)
      return(1);

   if(self->transmit_count)
      ret = (self->transmit_buffer[self->transmit_pos] >> self->bitpos) & 1;

   self->receive_buffer &= ~(1 << self->bitpos);
   self->receive_buffer |= TxD << self->bitpos;
   self->bitpos = (self->bitpos + 1) & 0x7;

   if(!self->bitpos)
   {
      /* printf("[PAD] Receive: %02x -- command_phase=%d\n", receive_buffer, command_phase); */

      if(self->transmit_count)
      {
         self->transmit_pos++;
         self->transmit_count--;
      }


      switch(self->command_phase)
      {
         case 0:
            if(self->receive_buffer != 0x01)
               self->command_phase = -1;
            else
            {
               self->transmit_buffer[0] = 0x63;
               self->transmit_pos = 0;
               self->transmit_count = 1;
               self->command_phase++;
            }
            break;

         case 2:
            /* if(receive_buffer) */
            /*  printf("%02x\n", receive_buffer); */
            break;

         case 1:
            self->command = self->receive_buffer;
            self->command_phase++;

            self->transmit_buffer[0] = 0x5A;

            /* puts("MOO"); */
            /* if(command != 0x42) */
            /*  fprintf(stderr, "GunCon unhandled command: 0x%02x\n", command); */
            /* assert(command == 0x42); */
            if(self->command == 0x42)
            {
               self->transmit_buffer[1] = 0xFF ^ ((self->buttons & 0x01) << 3);
               self->transmit_buffer[2] = 0xFF ^ (self->trigger_eff << 5) ^ ((self->buttons & 0x02) << 5);

               if(self->os_shot_counter > 0)
               {
                  self->hit_x = 0x01;
                  self->hit_y = 0x0A;
                  self->transmit_buffer[2] |= (1 << 5);
                  if(self->os_shot_counter == 2 || self->os_shot_counter == 3)
                  {
                     self->transmit_buffer[2] &= ~(1 << 5);
                  }
               }

               {
                  uint16_t _hx = (uint16_t)self->hit_x;
                  uint16_t _hy = (uint16_t)self->hit_y;
#ifdef MSB_FIRST
                  self->transmit_buffer[3] = (uint8_t)_hx;
                  self->transmit_buffer[4] = (uint8_t)(_hx >> 8);
                  self->transmit_buffer[5] = (uint8_t)_hy;
                  self->transmit_buffer[6] = (uint8_t)(_hy >> 8);
#else
                  memcpy(&self->transmit_buffer[3], &_hx, 2);
                  memcpy(&self->transmit_buffer[5], &_hy, 2);
#endif
               }

               self->hit_x = 0x01;
               self->hit_y = 0x0A;

               self->transmit_pos = 0;
               self->transmit_count = 7;

               self->trigger_eff = self->trigger_noclear;
            }
            else
            {
               self->command_phase = -1;
               self->transmit_buffer[1] = 0;
               self->transmit_buffer[2] = 0;
               self->transmit_pos = 0;
               self->transmit_count = 0;
            }
            break;

      }
   }

   if(!self->bitpos && self->transmit_count)
      *dsr_pulse_delay = 100; /* 0x80; //0x40; */

   return(ret);
}




/* ===========================================================================
 *  Merged from mednafen/psx/input/justifier.cpp
 * =========================================================================== */

typedef struct InputDevice_Justifier InputDevice_Justifier;
struct InputDevice_Justifier
{
   InputDevice base;
         bool dtr;
         uint8_t buttons;
         bool trigger_eff;
         bool trigger_noclear;
         bool need_hit_detect;
         int16_t nom_x, nom_y;
         int32_t os_shot_counter;
         bool prev_oss;
         int32_t command_phase;
         uint32_t bitpos;
         uint8_t receive_buffer;
         uint8_t command;
         uint8_t transmit_buffer[16];
         uint32_t transmit_pos;
         uint32_t transmit_count;
         bool prev_vsync;
         int line_counter;
};


static void InputDevice_Justifier_Ctor(InputDevice *self_)
{
   (void)self_;

   InputDevice_Justifier_Power(self_);
}



static void InputDevice_Justifier_Power(InputDevice *self_)
{
   InputDevice_Justifier *self = (InputDevice_Justifier *)self_;

   self->dtr = 0;

   self->buttons = 0;
   self->trigger_eff = 0;
   self->trigger_noclear = 0;

   self->need_hit_detect = false;

   self->nom_x = 0;
   self->nom_y = 0;

   self->os_shot_counter = 0;
   self->prev_oss = 0;

   self->command_phase = 0;

   self->bitpos = 0;

   self->receive_buffer = 0;

   self->command = 0;

   memset(self->transmit_buffer, 0, sizeof(self->transmit_buffer));

   self->transmit_pos = 0;
   self->transmit_count = 0;

   self->prev_vsync = 0;
   self->line_counter = 0;
}

static void InputDevice_Justifier_UpdateInput(InputDevice *self_, const void *data)
{
   InputDevice_Justifier *self = (InputDevice_Justifier *)self_;

   uint8_t *d8 = (uint8_t *)data;

   {
      uint16_t _x, _y;
#ifdef MSB_FIRST
      _x = (uint16_t)d8[0] | ((uint16_t)d8[1] << 8);
      _y = (uint16_t)d8[2] | ((uint16_t)d8[3] << 8);
#else
      memcpy(&_x, &d8[0], 2);
      memcpy(&_y, &d8[2], 2);
#endif
      self->nom_x = (int16_t)_x;
      self->nom_y = (int16_t)_y;
   }

   self->trigger_noclear = (bool)(d8[4] & 0x1);
   self->trigger_eff |= self->trigger_noclear;

   self->buttons = (d8[4] >> 1) & 0x3;

   if(self->os_shot_counter > 0)	/*  FIXME if UpdateInput() is ever called more than once per video frame(at ~50 or ~60Hz). */
      self->os_shot_counter--;

   if((d8[4] & 0x8) && !self->prev_oss && self->os_shot_counter == 0)
      self->os_shot_counter = 10;
   self->prev_oss = d8[4] & 0x8;
}

static int InputDevice_Justifier_StateAction(InputDevice *self_, StateMem* sm, int load, int data_only, const char* section_name)
{
   InputDevice_Justifier *self = (InputDevice_Justifier *)self_;

   SFORMAT StateRegs[] =
   {
      SFVAR(self->dtr),

      SFVAR(self->buttons),
      SFVAR(self->trigger_eff),
      SFVAR(self->trigger_noclear),

      SFVAR(self->need_hit_detect),

      SFVAR(self->nom_x),
      SFVAR(self->nom_y),
      SFVAR(self->os_shot_counter),
      SFVAR(self->prev_oss),

      SFVAR(self->command_phase),
      SFVAR(self->bitpos),
      SFVAR(self->receive_buffer),

      SFVAR(self->command),

      SFARRAY(self->transmit_buffer, sizeof(self->transmit_buffer)),
      SFVAR(self->transmit_pos),
      SFVAR(self->transmit_count),

      SFVAR(self->prev_vsync),
      SFVAR(self->line_counter),

      SFEND
   };
   int ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, section_name);

   if(load)
   {
      if(((uint64_t)self->transmit_pos + self->transmit_count) > sizeof(self->transmit_buffer))
      {
         self->transmit_pos = 0;
         self->transmit_count = 0;
      }
   }

   return(ret);
}

static int32_t InputDevice_Justifier_GPULineHook(InputDevice *self_, const int32_t timestamp, bool vsync, uint32_t *pixels, const unsigned width, const unsigned pix_clock_offset, const unsigned pix_clock, const unsigned pix_clock_divider, const unsigned surf_pitchinpix, const unsigned upscale_factor)
{
   InputDevice_Justifier *self = (InputDevice_Justifier *)self_;

   int32_t ret = PSX_EVENT_MAXTS;

   if(vsync && !self->prev_vsync)
      self->line_counter = 0;

   if(pixels && pix_clock)
   {
      const int avs = 16; /*  Not 16 for PAL, fixme. */
      int32_t gx;
      int32_t gy;
      int32_t gxa;

      gx = (self->nom_x * 2 + pix_clock_divider) / (pix_clock_divider * 2);
      gy = self->nom_y;
      gxa = gx; /*  - (pix_clock / 400000); */
      /* if(gxa < 0 && gx >= 0) */
      /*  gxa = 0; */

      if(!self->os_shot_counter && self->need_hit_detect && gxa >= 0 && gxa < (int)width && self->line_counter >= (avs + gy - 1) && self->line_counter <= (avs + gy + 1))
      {
         int r, g, b, a;

         MDFN_DecodeColor(pixels[gxa * upscale_factor], &r, &g, &b, &a);

         if((r + g + b) >= 0x40)	/*  Wrong, but not COMPLETELY ABSOLUTELY wrong, at least. ;) */
         {
            ret = timestamp + (int64_t)(gxa + pix_clock_offset) * (44100 * 768) / pix_clock - 177;
         }
      }

      self->base.chair_x = gx;
      self->base.chair_y = (avs + gy) - self->line_counter;
   }

   self->line_counter++;

   return(ret);
}

static void InputDevice_Justifier_SetDTR(InputDevice *self_, bool new_dtr)
{
   InputDevice_Justifier *self = (InputDevice_Justifier *)self_;

   if(!self->dtr && new_dtr)
   {
      self->command_phase = 0;
      self->bitpos = 0;
      self->transmit_pos = 0;
      self->transmit_count = 0;
   }
   else if(self->dtr && !new_dtr)
   {
      /* if(bitpos || transmit_count) */
      /*  printf("[PAD] Abort communication!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"); */
   }

   self->dtr = new_dtr;
}

static bool InputDevice_Justifier_GetDSR(InputDevice *self_)
{
   InputDevice_Justifier *self = (InputDevice_Justifier *)self_;

   if(!self->dtr)
      return(0);

   if(!self->bitpos && self->transmit_count)
      return(1);

   return(0);
}

static bool InputDevice_Justifier_Clock(InputDevice *self_, bool TxD, int32_t *dsr_pulse_delay)
{
   InputDevice_Justifier *self = (InputDevice_Justifier *)self_;

   bool ret = 1;

   *dsr_pulse_delay = 0;

   if(!self->dtr)
      return(1);

   if(self->transmit_count)
      ret = (self->transmit_buffer[self->transmit_pos] >> self->bitpos) & 1;

   self->receive_buffer &= ~(1 << self->bitpos);
   self->receive_buffer |= TxD << self->bitpos;
   self->bitpos = (self->bitpos + 1) & 0x7;

   if(!self->bitpos)
   {
      /* printf("[PAD] Receive: %02x -- command_phase=%d\n", receive_buffer, command_phase); */

      if(self->transmit_count)
      {
         self->transmit_pos++;
         self->transmit_count--;
      }


      switch(self->command_phase)
      {
         case 0:
            if(self->receive_buffer != 0x01)
               self->command_phase = -1;
            else
            {
               self->transmit_buffer[0] = 0x31;
               self->transmit_pos = 0;
               self->transmit_count = 1;
               self->command_phase++;
            }
            break;

         case 2:
            /* if(receive_buffer) */
            /*  printf("%02x\n", receive_buffer); */
            self->command_phase++;
            break;

         case 3:
            self->need_hit_detect = self->receive_buffer & 0x10;	/*  TODO, see if it's (val&0x10) == 0x10, or some other mask value. */
            self->command_phase++;
            break;

         case 1:
            self->command = self->receive_buffer;
            self->command_phase++;

            self->transmit_buffer[0] = 0x5A;

            /* if(command != 0x42) */
            /*  fprintf(stderr, "Justifier unhandled command: 0x%02x\n", command); */
            /* assert(command == 0x42); */
            if(self->command == 0x42)
            {
               self->transmit_buffer[1] = 0xFF ^ ((self->buttons & 2) << 2);
               self->transmit_buffer[2] = 0xFF ^ (self->trigger_eff << 7) ^ ((self->buttons & 1) << 6);

               if(self->os_shot_counter > 0)
               {
                  self->transmit_buffer[2] |= (1 << 7);
                  if(self->os_shot_counter == 6 || self->os_shot_counter == 5)
                  {
                     self->transmit_buffer[2] &= ~(1 << 7);
                  }
               }

               self->transmit_pos = 0;
               self->transmit_count = 3;

               self->trigger_eff = self->trigger_noclear;
            }
            else
            {
               self->command_phase = -1;
               self->transmit_buffer[1] = 0;
               self->transmit_buffer[2] = 0;
               self->transmit_pos = 0;
               self->transmit_count = 0;
            }
            break;

      }
   }

   if(!self->bitpos && self->transmit_count)
      *dsr_pulse_delay = 200;

   return(ret);
}




/* ===========================================================================
 *  Merged from mednafen/psx/input/memcard.cpp
 * =========================================================================== */

typedef struct InputDevice_Memcard InputDevice_Memcard;
struct InputDevice_Memcard
{
   InputDevice base;
         bool presence_new;
         uint8_t card_data[1 << 17];
         uint8_t rw_buffer[128];
         uint8_t write_xor;
         bool data_used;
         uint64_t dirty_count;
         bool dtr;
         int32_t command_phase;
         uint32_t bitpos;
         uint8_t receive_buffer;
         uint8_t command;
         uint16_t addr;
         uint8_t calced_xor;
         uint8_t transmit_buffer;
         uint32_t transmit_count;
};


static void InputDevice_Memcard_Format(InputDevice *self_)
{
   unsigned int A;
   InputDevice_Memcard *self = (InputDevice_Memcard *)self_;

   memset(self->card_data, 0x00, sizeof(self->card_data));

   self->card_data[0x00] = 0x4D;
   self->card_data[0x01] = 0x43;
   self->card_data[0x7F] = 0x0E;

   for (A = 0x80; A < 0x800; A += 0x80)
   {
      self->card_data[A + 0x00] = 0xA0;
      self->card_data[A + 0x08] = 0xFF;
      self->card_data[A + 0x09] = 0xFF;
      self->card_data[A + 0x7F] = 0xA0;
   }

   for (A = 0x0800; A < 0x1200; A += 0x80)
   {
      self->card_data[A + 0x00] = 0xFF;
      self->card_data[A + 0x01] = 0xFF;
      self->card_data[A + 0x02] = 0xFF;
      self->card_data[A + 0x03] = 0xFF;
      self->card_data[A + 0x08] = 0xFF;
      self->card_data[A + 0x09] = 0xFF;
   }
}

static void InputDevice_Memcard_Ctor(InputDevice *self_)
{
   InputDevice_Memcard *self = (InputDevice_Memcard *)self_;

   InputDevice_Memcard_Power(self_);

   self->data_used = false;
   self->dirty_count = 0;

   /*  Init memcard as formatted. */
   assert(sizeof(self->card_data) == (1 << 17));
   InputDevice_Memcard_Format(self_);
}



static void InputDevice_Memcard_Power(InputDevice *self_)
{
   InputDevice_Memcard *self = (InputDevice_Memcard *)self_;

   self->presence_new = true;
   memset(self->rw_buffer, 0, sizeof(self->rw_buffer));
   self->write_xor = 0;

   self->dtr = 0;
   self->command_phase = 0;

   self->bitpos = 0;

   self->receive_buffer = 0;

   self->command = 0;
   self->addr = 0;
   self->calced_xor = 0;

   self->transmit_buffer = 0;

   self->transmit_count = 0;
}

static int InputDevice_Memcard_StateAction(InputDevice *self_, StateMem* sm, int load, int data_only, const char* section_name)
{
   InputDevice_Memcard *self = (InputDevice_Memcard *)self_;

   /*  Don't save dirty_count. */
   SFORMAT StateRegs[] =
   {
      SFVAR(self->presence_new),

      SFARRAY(self->rw_buffer, sizeof(self->rw_buffer)),
      SFVAR(self->write_xor),

      SFVAR(self->dtr),
      SFVAR(self->command_phase),
      SFVAR(self->bitpos),
      SFVAR(self->receive_buffer),

      SFVAR(self->command),
      SFVAR(self->addr),
      SFVAR(self->calced_xor),

      SFVAR(self->transmit_buffer),
      SFVAR(self->transmit_count),

      SFVAR(self->data_used),

      SFEND
   };

   SFORMAT CD_StateRegs[] =
   {
      SFARRAY(self->card_data, sizeof(self->card_data)),
      SFEND
   };
   int ret = 1;

   if(MDFNSS_StateAction(sm, load, data_only, StateRegs, section_name) != 0)
   {
      /* printf("%s data_used=%d\n", section_name, data_used); */
      if(self->data_used)
      {
         char tmp_name[64];
         snprintf(tmp_name, sizeof(tmp_name), "%s_DT", section_name);

         ret &= MDFNSS_StateAction(sm, load, data_only, CD_StateRegs, tmp_name);
      }

      if(load)
      {
         if(self->data_used)
            self->dirty_count++;
      }
   }
   else
      ret = 0;

   return(ret);
}

static void InputDevice_Memcard_SetDTR(InputDevice *self_, bool new_dtr)
{
   InputDevice_Memcard *self = (InputDevice_Memcard *)self_;

   if(!self->dtr && new_dtr)
   {
      self->command_phase = 0;
      self->bitpos = 0;
      self->transmit_count = 0;
   }
   self->dtr = new_dtr;
}

static bool InputDevice_Memcard_GetDSR(InputDevice *self_)
{
   InputDevice_Memcard *self = (InputDevice_Memcard *)self_;

   if(!self->dtr)
      return(0);

   if(!self->bitpos && self->transmit_count)
      return(1);

   return(0);
}

static bool InputDevice_Memcard_Clock(InputDevice *self_, bool TxD, int32_t *dsr_pulse_delay)
{
   InputDevice_Memcard *self = (InputDevice_Memcard *)self_;

   bool ret = 1;

   *dsr_pulse_delay = 0;

   if(!self->dtr)
      return(1);

   if(self->transmit_count)
      ret = (self->transmit_buffer >> self->bitpos) & 1;

   self->receive_buffer &= ~(1 << self->bitpos);
   self->receive_buffer |= TxD << self->bitpos;
   self->bitpos = (self->bitpos + 1) & 0x7;

   if(!self->bitpos)
   {
      /* if(command_phase > 0 || transmit_count) */
      /*  printf("[MCRDATA] Received_data=0x%02x, Sent_data=0x%02x\n", receive_buffer, transmit_buffer); */

      if(self->transmit_count)
      {
         self->transmit_count--;
      }

      if (self->command_phase >= 1024 && self->command_phase <= 1151)
      {
         /*  Transmit actual 128 bytes data */
         self->transmit_buffer = self->card_data[(self->addr << 7) + (self->command_phase - 1024)];
         self->calced_xor ^= self->transmit_buffer;
         self->transmit_count = 1;
         self->command_phase++;
      }
      else if (self->command_phase >= 2048 && self->command_phase <= 2175)
      {
         self->calced_xor ^= self->receive_buffer;
         self->rw_buffer[self->command_phase - 2048] = self->receive_buffer;

         self->transmit_buffer = self->receive_buffer;
         self->transmit_count = 1;
         self->command_phase++;
      }
      else
         switch(self->command_phase)
         {
            case 0:
               if(self->receive_buffer != 0x81)
                  self->command_phase = -1;
               else
               {
                  /* printf("[MCR] Device selected\n"); */
                  self->transmit_buffer = self->presence_new ? 0x08 : 0x00;
                  self->transmit_count = 1;
                  self->command_phase++;
               }
               break;

            case 1:
               self->command = self->receive_buffer;
               /* printf("[MCR] Command received: %c\n", command); */
               if(self->command == 'R' || self->command == 'W')
               {
                  self->command_phase++;
                  self->transmit_buffer = 0x5A;
                  self->transmit_count = 1;
               }
               else
               {
                  self->command_phase = -1;
                  self->transmit_buffer = 0;
                  self->transmit_count = 0;
               }
               break;

            case 2:
               self->transmit_buffer = 0x5D;
               self->transmit_count = 1;
               self->command_phase++;
               break;

            case 3:
               self->transmit_buffer = 0x00;
               self->transmit_count = 1;
               if(self->command == 'R')
                  self->command_phase = 1000;
               else if(self->command == 'W')
                  self->command_phase = 2000;
               break;

               /*  */
               /*  Read */
               /*  */
            case 1000:
               self->addr = self->receive_buffer << 8;
               self->transmit_buffer = self->receive_buffer;
               self->transmit_count = 1;
               self->command_phase++;
               break;

            case 1001:
               self->addr |= self->receive_buffer & 0xFF;
               self->transmit_buffer = '\\';
               self->transmit_count = 1;
               self->command_phase++;
               break;

            case 1002:
               /* printf("[MCR]   READ ADDR=0x%04x\n", addr); */
               if(self->addr >= (sizeof(self->card_data) >> 7))
                  self->addr = 0xFFFF;

               self->calced_xor = 0;
               self->transmit_buffer = ']';
               self->transmit_count = 1;
               self->command_phase++;

               /*  TODO: enable this code(or something like it) when CPU instruction timing is a bit better. */
               /*  */
               /* *dsr_pulse_delay = 32000; */
               /* goto SkipDPD; */
               /*  */

               break;

            case 1003:
               self->transmit_buffer = self->addr >> 8;
               self->calced_xor ^= self->transmit_buffer;
               self->transmit_count = 1;
               self->command_phase++;
               break;

            case 1004:
               self->transmit_buffer = self->addr & 0xFF;
               self->calced_xor ^= self->transmit_buffer;

               if(self->addr == 0xFFFF)
               {
                  self->transmit_count = 1;
                  self->command_phase = -1;
               }
               else
               {
                  self->transmit_count = 1;
                  self->command_phase = 1024;
               }
               break;



               /*  XOR */
            case (1024 + 128):
               self->transmit_buffer = self->calced_xor;
               self->transmit_count = 1;
               self->command_phase++;
               break;

               /*  End flag */
            case (1024 + 129):
               self->transmit_buffer = 'G';
               self->transmit_count = 1;
               self->command_phase = -1;
               break;

               /*  */
               /*  Write */
               /*  */
            case 2000:
               self->calced_xor = self->receive_buffer;
               self->addr = self->receive_buffer << 8;
               self->transmit_buffer = self->receive_buffer;
               self->transmit_count = 1;
               self->command_phase++;
               break;

            case 2001:
               self->calced_xor ^= self->receive_buffer;
               self->addr |= self->receive_buffer & 0xFF;
               /* printf("[MCR]   WRITE ADDR=0x%04x\n", addr); */
               self->transmit_buffer = self->receive_buffer;
               self->transmit_count = 1;
               self->command_phase = 2048;
               break;
            case (2048 + 128):	/*  XOR */
               self->write_xor = self->receive_buffer;
               self->transmit_buffer = '\\';
               self->transmit_count = 1;
               self->command_phase++;
               break;

            case (2048 + 129):
               self->transmit_buffer = ']';
               self->transmit_count = 1;
               self->command_phase++;
               break;

            case (2048 + 130):	/*  End flag */
               /* printf("[MCR] Write End.  Actual_XOR=0x%02x, CW_XOR=0x%02x\n", calced_xor, write_xor); */

               if(self->calced_xor != self->write_xor)
                  self->transmit_buffer = 'N';
               else if(self->addr >= (sizeof(self->card_data) >> 7))
                  self->transmit_buffer = 0xFF;
               else
               {
                  self->transmit_buffer = 'G';
                  self->presence_new = false;

                  /*  If the current data is different from the data to be written, increment the dirty count. */
                  /*  memcpy()'ing over to card_data is also conditionalized here for a slight optimization. */
                  if(memcmp(&self->card_data[self->addr << 7], self->rw_buffer, 128))
                  {
                     memcpy(&self->card_data[self->addr << 7], self->rw_buffer, 128);
                     self->dirty_count++;
                     self->data_used = true;
                  }
               }

               self->transmit_count = 1;
               self->command_phase = -1;
               break;

         }

      /* if(command_phase != -1 || transmit_count) */
      /*  printf("[MCR] Receive: 0x%02x, Send: 0x%02x -- %d\n", receive_buffer, transmit_buffer, command_phase); */
   }

   if(!self->bitpos && self->transmit_count)
      *dsr_pulse_delay = 0x100;

   /* SkipDPD: ; */

   return(ret);
}

static uint8_t * InputDevice_Memcard_GetNVData(InputDevice *self_)
{
   InputDevice_Memcard *self = (InputDevice_Memcard *)self_;

   return self->card_data;
}

static uint32_t InputDevice_Memcard_GetNVSize(InputDevice *self_)
{
   InputDevice_Memcard *self = (InputDevice_Memcard *)self_;

   return(sizeof(self->card_data));
}

static void InputDevice_Memcard_ReadNV(InputDevice *self_, uint8_t *buffer, uint32_t offset, uint32_t size)
{
   InputDevice_Memcard *self = (InputDevice_Memcard *)self_;

   while(size--)
   {
      *buffer = self->card_data[offset & (sizeof(self->card_data) - 1)];
      buffer++;
      offset++;
   }
}

static void InputDevice_Memcard_WriteNV(InputDevice *self_, const uint8_t *buffer, uint32_t offset, uint32_t size)
{
   InputDevice_Memcard *self = (InputDevice_Memcard *)self_;

   if(size)
   {
      self->dirty_count++;
   }

   while(size--)
   {
      if(self->card_data[offset & (sizeof(self->card_data) - 1)] != *buffer)
         self->data_used = true;

      self->card_data[offset & (sizeof(self->card_data) - 1)] = *buffer;
      buffer++;
      offset++;
   }
}

static uint64_t InputDevice_Memcard_GetNVDirtyCount(InputDevice *self_)
{
   InputDevice_Memcard *self = (InputDevice_Memcard *)self_;

   return(self->dirty_count);
}

static void InputDevice_Memcard_ResetNVDirtyCount(InputDevice *self_)
{
   InputDevice_Memcard *self = (InputDevice_Memcard *)self_;

   self->dirty_count = 0;
}




void Device_Memcard_Power(InputDevice *device)
{
   InputDevice_Memcard *memcard = (InputDevice_Memcard *)device;
   if (memcard)
      InputDevice_Memcard_Power(&memcard->base);
}

void Device_Memcard_Format(InputDevice *device)
{
   /* Type already known by caller convention. */
   InputDevice_Memcard *memcard = (InputDevice_Memcard *)device;
   if (memcard)
      InputDevice_Memcard_Format(&memcard->base);
}


/* ===========================================================================
 *  Merged from mednafen/psx/input/multitap.cpp
 * =========================================================================== */

/*
TODO: PS1 multitap appears to have some internal knowledge of controller IDs, so it won't get "stuck" waiting for data from a controller that'll never
come.  We currently sort of "cheat" due to how the dsr_pulse_delay stuff works, but in the future we should try to emulate this multitap functionality.

Also, full-mode read startup and subport controller ID read timing isn't quite right, so we should fix that too.
*/

/*
   Notes from tests on real thing(not necessarily emulated the same way here):

   Manual port selection read mode:
   Write 0x01-0x04 instead of 0x01 as first byte, selects port(1=A,2=B,3=C,4=D) to access.

   Ports that don't exist(0x00, 0x05-0xFF) or don't have a device plugged in will not respond(no DSR pulse).

   Full read mode:
   Bit0 of third byte(from-zero-index=0x02) should be set to 1 to enter full read mode, on subsequent reads.

   Appears to require a controller to be plugged into the port specified by the first byte as per manual port selection read mode,
   to write the byte necessary to enter full-read mode; but once the third byte with the bit set has been written, no controller in
   that port is required for doing full reads(and the manual port selection is ignored when doing a full read).

   However, if there are no controllers plugged in, the returned data will be short:
   % 0: 0xff
   % 1: 0x80
   % 2: 0x5a

   Example full-read bytestream(with controllers plugged into port A, port B, and port C, with port D empty):
   % 0: 0xff
   % 1: 0x80
   % 2: 0x5a

   % 3: 0x73	(Port A controller data start)
   % 4: 0x5a
   % 5: 0xff
   % 6: 0xff
   % 7: 0x80
   % 8: 0x8c
   % 9: 0x79
   % 10: 0x8f

   % 11: 0x53	(Port B controller data start)
   % 12: 0x5a
   % 13: 0xff
   % 14: 0xff
   % 15: 0x80
   % 16: 0x80
   % 17: 0x75
   % 18: 0x8e

   % 19: 0x41	(Port C controller data start)
   % 20: 0x5a
   % 21: 0xff
   % 22: 0xff
   % 23: 0xff
   % 24: 0xff
   % 25: 0xff
   % 26: 0xff

   % 27: 0xff	(Port D controller data start)
   % 28: 0xff
   % 29: 0xff
   % 30: 0xff
   % 31: 0xff
   % 32: 0xff
   % 33: 0xff
   % 34: 0xff

*/

static void InputDevice_Multitap_Ctor(InputDevice *self_)
{
   int i;
   InputDevice_Multitap *self = (InputDevice_Multitap *)self_;

   for (i = 0; i < 4; i++)
   {
      self->pad_devices[i] = NULL;
      self->mc_devices[i] = NULL;
   }
   InputDevice_Multitap_Power(self_);
}



static void InputDevice_Multitap_SetSubDevice(InputDevice *self_, unsigned int sub_index, InputDevice *device, InputDevice *mc_device)
{
   InputDevice_Multitap *self = (InputDevice_Multitap *)self_;

   assert(sub_index < 4);

   /* printf("%d\n", sub_index); */

   self->pad_devices[sub_index] = device;
   self->mc_devices[sub_index] = mc_device;
}


static void InputDevice_Multitap_Power(InputDevice *self_)
{
   int i;
   InputDevice_Multitap *self = (InputDevice_Multitap *)self_;

   self->selected_device = -1;
   self->bit_counter = 0;
   self->receive_buffer = 0;
   self->byte_counter = 0;

   self->mc_mode = false;
   self->full_mode = false;
   self->full_mode_setting = false;

   self->prev_fm_success = false;
   memset(self->sb, 0, sizeof(self->sb));

   self->fm_dp = 0;
   memset(self->fm_buffer, 0, sizeof(self->fm_buffer));
   self->fm_command_error = false;

   for (i = 0; i < 4; i++)
   {
      if(self->pad_devices[i])
         (self->pad_devices[i])->vt->Power((self->pad_devices[i]));

      if(self->mc_devices[i])
         (self->mc_devices[i])->vt->Power((self->mc_devices[i]));
   } 
}

static int InputDevice_Multitap_StateAction(InputDevice *self_, StateMem* sm, int load, int data_only, const char* section_name)
{
   InputDevice_Multitap *self = (InputDevice_Multitap *)self_;

   SFORMAT StateRegs[] =
   {
      SFVAR(self->dtr),

      SFVAR(self->selected_device),
      SFVAR(self->full_mode_setting),

      SFVAR(self->full_mode),
      SFVAR(self->mc_mode),

      SFVAR(self->prev_fm_success),

      SFVAR(self->fm_dp),
      SFARRAY(&self->fm_buffer[0][0], sizeof(self->fm_buffer) / sizeof(self->fm_buffer[0][0])),
      SFARRAY(&self->sb[0][0], sizeof(self->sb) / sizeof(self->sb[0][0])),

      SFVAR(self->fm_command_error),

      SFVAR(self->command),
      SFVAR(self->receive_buffer),
      SFVAR(self->bit_counter),
      SFVAR(self->byte_counter),

      SFEND
   };
   int ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, section_name);

   if(load)
   {

   }

   return(ret);
}

static void InputDevice_Multitap_SetDTR(InputDevice *self_, bool new_dtr)
{
   int i;
   InputDevice_Multitap *self = (InputDevice_Multitap *)self_;

   bool old_dtr = self->dtr;
   self->dtr = new_dtr;

   if(!self->dtr)
   {
      if(old_dtr)
      {
         /* printf("Multitap stop.\n"); */
      }

      self->bit_counter = 0;
      self->receive_buffer = 0;
      self->selected_device = -1;
      self->mc_mode = false;
      self->full_mode = false;
   }

   if(!old_dtr && self->dtr)
   {
      self->full_mode = self->full_mode_setting;

      if(!self->prev_fm_success)
      {
         unsigned i;
         memset(self->sb, 0, sizeof(self->sb));
         for(i = 0; i < 4; i++)
            self->sb[i][0] = 0x42;
      }

      self->prev_fm_success = false;

      self->byte_counter = 0;

      /* if(full_mode) */
      /*  printf("Multitap start: %d\n", full_mode); */
   }

   for (i = 0; i < 4; i++)
   {
      (self->pad_devices[i])->vt->SetDTR((self->pad_devices[i]), self->dtr);
      (self->mc_devices[i])->vt->SetDTR((self->mc_devices[i]), self->dtr);
   }
}

static bool InputDevice_Multitap_GetDSR(InputDevice *self_)
{
   (void)self_;

   return(0);
}

static bool InputDevice_Multitap_Clock(InputDevice *self_, bool TxD, int32_t *dsr_pulse_delay)
{
   unsigned i;
   InputDevice_Multitap *self = (InputDevice_Multitap *)self_;
   bool ret = 1;
   int32_t tmp_pulse_delay[2][4] = { { 0, 0, 0, 0 }, { 0, 0, 0, 0 } };

   if(!self->dtr)
      return(1);

   /* printf("Receive bit: %d\n", TxD); */
   /* printf("TxD %d\n", TxD); */

   self->receive_buffer &= ~ (1 << self->bit_counter);
   self->receive_buffer |= TxD << self->bit_counter;

   if(1)
   {
      if(self->byte_counter == 0)
      {
         bool mangled_txd = TxD;

         if(self->bit_counter < 4)
            mangled_txd = (0x01 >> self->bit_counter) & 1;

         for (i = 0; i < 4; i++)
         {
            (self->pad_devices[i])->vt->Clock((self->pad_devices[i]), mangled_txd, &tmp_pulse_delay[0][i]);
            (self->mc_devices[i])->vt->Clock((self->mc_devices[i]), mangled_txd, &tmp_pulse_delay[1][i]);
         }
      }
      else
      {
         if(self->full_mode)
         {
            if(self->byte_counter == 1)
               ret = (0x80 >> self->bit_counter) & 1;
            else if(self->byte_counter == 2)
               ret = (0x5A >> self->bit_counter) & 1;
            else if(self->byte_counter >= 0x03 && self->byte_counter < 0x03 + 0x08 * 4)
            {
               if(!self->fm_command_error && self->byte_counter < (0x03 + 0x08))
               {
                  unsigned i;
                  for(i = 0; i < 4; i++)
                  { 
                     self->fm_buffer[i][self->byte_counter - 0x03] &= ((self->pad_devices[i])->vt->Clock((self->pad_devices[i]), (self->sb[i][self->byte_counter - 0x03] >> self->bit_counter) & 1, &tmp_pulse_delay[0][i]) << self->bit_counter) | (~(1U << self->bit_counter));
                  }
               }
               ret &= ((&self->fm_buffer[0][0])[self->byte_counter - 0x03] >> self->bit_counter) & 1;
            }
         }
         else /*  to if(full_mode) */
         {
            if((unsigned)self->selected_device < 4)
            {
               ret &= (self->pad_devices[self->selected_device])->vt->Clock((self->pad_devices[self->selected_device]), TxD, &tmp_pulse_delay[0][self->selected_device]);
               ret &= (self->mc_devices[self->selected_device])->vt->Clock((self->mc_devices[self->selected_device]), TxD, &tmp_pulse_delay[1][self->selected_device]);
            }
         }
      } /*  end else to if(byte_counter == 0) */
   }

   /*  */
   /*  */
   /*  */

   self->bit_counter = (self->bit_counter + 1) & 0x7;
   if(self->bit_counter == 0)
   {
      /* printf("MT Receive: 0x%02x\n", receive_buffer); */
      if(self->byte_counter == 0)
      {
         self->mc_mode = (bool)(self->receive_buffer & 0xF0);
         if(self->mc_mode)
            self->full_mode = false;

         /* printf("Zoomba: 0x%02x\n", receive_buffer); */
         /* printf("Full mode: %d %d %d\n", full_mode, bit_counter, byte_counter); */

         if(self->full_mode)
         {
            memset(self->fm_buffer, 0xFF, sizeof(self->fm_buffer));
            self->selected_device = 0;
         }
         else
         {
            /* printf("Device select: %02x\n", receive_buffer); */
            self->selected_device = ((self->receive_buffer & 0xF) - 1) & 0xFF;
         }
      }

      if(self->byte_counter == 1)
      {
         self->command = self->receive_buffer;
         self->fm_command_error = false;

         /* printf("Multitap sub-command: %02x\n", command); */

         if(self->full_mode && self->command != 0x42)
            self->fm_command_error = true;
      }

      if((!self->mc_mode || self->full_mode) && self->byte_counter == 2)
      {
         /* printf("Full mode setting: %02x\n", receive_buffer); */
         self->full_mode_setting = self->receive_buffer & 0x01;
      }

      if(self->full_mode)
      {
         if(self->byte_counter >= 3 + 8 * 0 && self->byte_counter < (3 + 8 * 4))
         {
            const unsigned adjbi = self->byte_counter - 3;
            self->sb[adjbi >> 3][adjbi & 0x7] = self->receive_buffer;
         }

         if(self->byte_counter == 33)
            self->prev_fm_success = true;
      }

      /*  Handle DSR stuff */
      if(self->full_mode)
      {
         if(self->byte_counter == 0)	/*  Next byte: 0x80 */
         {
            *dsr_pulse_delay = 1000;

            self->fm_dp = 0;
            for (i = 0; i < 4; i++)
               self->fm_dp |= (((bool)(tmp_pulse_delay[0][i])) << i);
         }
         else if(self->byte_counter == 1)	/*  Next byte: 0x5A */
            *dsr_pulse_delay = 0x40;
         else if(self->byte_counter == 2)	/*  Next byte(typically, controller-dependent): 0x41 */
         {
            if(self->fm_dp)
               *dsr_pulse_delay = 0x40;
            else
            {
               self->byte_counter = 255;
               *dsr_pulse_delay = 0;
            }
         }
         else if(self->byte_counter >= 3 && self->byte_counter < 34)	/*  Next byte when byte_counter==3 (typically, controller-dependent): 0x5A */
         {
            if(self->byte_counter < 10)
            { 
               unsigned i;
               int d = 0x40;

               for(i = 0; i < 4; i++)
               {
                  int32_t tpd = tmp_pulse_delay[0][i];

                  if(self->byte_counter == 3 && (self->fm_dp & (1U << i)) && tpd == 0)
                  {
                     /* printf("SNORG: %u %02x\n", i, sb[i][0]); */
                     self->fm_command_error = true;
                  }

                  if(tpd > d)
                     d = tpd;
               }

               *dsr_pulse_delay = d;
            }
            else
               *dsr_pulse_delay = 0x20;

            if(self->byte_counter == 3 && self->fm_command_error)
            {
               self->byte_counter = 255;
               *dsr_pulse_delay = 0;
            }
         }
      } /*  end if(full_mode) */
      else
      {
         if((unsigned)self->selected_device < 4)
         {
            uint32_t _a = tmp_pulse_delay[0][self->selected_device];
            uint32_t _b = tmp_pulse_delay[1][self->selected_device];
            *dsr_pulse_delay = (_a > _b) ? _a : _b;
         }
      }


      /*  */
      /*  */
      /*  */

      /* printf("Byte Counter Increment\n"); */
      if(self->byte_counter < 255)
         self->byte_counter++;
   }



   return(ret);
}

/* Vtables for each device type. */

static const InputDevice_VTable InputDevice_Gamepad_vtable = {
   /* .Power = */ InputDevice_Gamepad_Power,
   /* .UpdateInput = */ InputDevice_Gamepad_UpdateInput,
   /* .StateAction = */ InputDevice_Gamepad_StateAction,
   /* .GPULineHook = */ InputDevice_GPULineHook,
   /* .Update = */ InputDevice_Update,
   /* .ResetTS = */ InputDevice_ResetTS,
   /* .SetAMCT = */ InputDevice_SetAMCT,
   /* .SetCrosshairsCursor = */ InputDevice_SetCrosshairsCursor,
   /* .SetCrosshairsColor = */ InputDevice_SetCrosshairsColor,
   /* .SetDTR = */ InputDevice_Gamepad_SetDTR,
   /* .GetDSR = */ InputDevice_Gamepad_GetDSR,
   /* .Clock = */ InputDevice_Gamepad_Clock,
   /* .GetNVData = */ InputDevice_GetNVData,
   /* .GetNVSize = */ InputDevice_GetNVSize,
   /* .ReadNV = */ InputDevice_ReadNV,
   /* .WriteNV = */ InputDevice_WriteNV,
   /* .GetNVDirtyCount = */ InputDevice_GetNVDirtyCount,
   /* .ResetNVDirtyCount = */ InputDevice_ResetNVDirtyCount,
   /* .Destroy = */ NULL,
};

static const InputDevice_VTable InputDevice_DualAnalog_vtable = {
   /* .Power = */ InputDevice_DualAnalog_Power,
   /* .UpdateInput = */ InputDevice_DualAnalog_UpdateInput,
   /* .StateAction = */ InputDevice_DualAnalog_StateAction,
   /* .GPULineHook = */ InputDevice_GPULineHook,
   /* .Update = */ InputDevice_Update,
   /* .ResetTS = */ InputDevice_ResetTS,
   /* .SetAMCT = */ InputDevice_SetAMCT,
   /* .SetCrosshairsCursor = */ InputDevice_SetCrosshairsCursor,
   /* .SetCrosshairsColor = */ InputDevice_SetCrosshairsColor,
   /* .SetDTR = */ InputDevice_DualAnalog_SetDTR,
   /* .GetDSR = */ InputDevice_DualAnalog_GetDSR,
   /* .Clock = */ InputDevice_DualAnalog_Clock,
   /* .GetNVData = */ InputDevice_GetNVData,
   /* .GetNVSize = */ InputDevice_GetNVSize,
   /* .ReadNV = */ InputDevice_ReadNV,
   /* .WriteNV = */ InputDevice_WriteNV,
   /* .GetNVDirtyCount = */ InputDevice_GetNVDirtyCount,
   /* .ResetNVDirtyCount = */ InputDevice_ResetNVDirtyCount,
   /* .Destroy = */ NULL,
};

static const InputDevice_VTable InputDevice_DualShock_vtable = {
   /* .Power = */ InputDevice_DualShock_Power,
   /* .UpdateInput = */ InputDevice_DualShock_UpdateInput,
   /* .StateAction = */ InputDevice_DualShock_StateAction,
   /* .GPULineHook = */ InputDevice_GPULineHook,
   /* .Update = */ InputDevice_DualShock_Update,
   /* .ResetTS = */ InputDevice_DualShock_ResetTS,
   /* .SetAMCT = */ InputDevice_DualShock_SetAMCT,
   /* .SetCrosshairsCursor = */ InputDevice_SetCrosshairsCursor,
   /* .SetCrosshairsColor = */ InputDevice_SetCrosshairsColor,
   /* .SetDTR = */ InputDevice_DualShock_SetDTR,
   /* .GetDSR = */ InputDevice_DualShock_GetDSR,
   /* .Clock = */ InputDevice_DualShock_Clock,
   /* .GetNVData = */ InputDevice_GetNVData,
   /* .GetNVSize = */ InputDevice_GetNVSize,
   /* .ReadNV = */ InputDevice_ReadNV,
   /* .WriteNV = */ InputDevice_WriteNV,
   /* .GetNVDirtyCount = */ InputDevice_GetNVDirtyCount,
   /* .ResetNVDirtyCount = */ InputDevice_ResetNVDirtyCount,
   /* .Destroy = */ NULL,
};

static const InputDevice_VTable InputDevice_Mouse_vtable = {
   /* .Power = */ InputDevice_Mouse_Power,
   /* .UpdateInput = */ InputDevice_Mouse_UpdateInput,
   /* .StateAction = */ InputDevice_Mouse_StateAction,
   /* .GPULineHook = */ InputDevice_GPULineHook,
   /* .Update = */ InputDevice_Mouse_Update,
   /* .ResetTS = */ InputDevice_Mouse_ResetTS,
   /* .SetAMCT = */ InputDevice_SetAMCT,
   /* .SetCrosshairsCursor = */ InputDevice_SetCrosshairsCursor,
   /* .SetCrosshairsColor = */ InputDevice_SetCrosshairsColor,
   /* .SetDTR = */ InputDevice_Mouse_SetDTR,
   /* .GetDSR = */ InputDevice_GetDSR,
   /* .Clock = */ InputDevice_Mouse_Clock,
   /* .GetNVData = */ InputDevice_GetNVData,
   /* .GetNVSize = */ InputDevice_GetNVSize,
   /* .ReadNV = */ InputDevice_ReadNV,
   /* .WriteNV = */ InputDevice_WriteNV,
   /* .GetNVDirtyCount = */ InputDevice_GetNVDirtyCount,
   /* .ResetNVDirtyCount = */ InputDevice_ResetNVDirtyCount,
   /* .Destroy = */ NULL,
};

static const InputDevice_VTable InputDevice_neGcon_vtable = {
   /* .Power = */ InputDevice_neGcon_Power,
   /* .UpdateInput = */ InputDevice_neGcon_UpdateInput,
   /* .StateAction = */ InputDevice_StateAction,
   /* .GPULineHook = */ InputDevice_GPULineHook,
   /* .Update = */ InputDevice_Update,
   /* .ResetTS = */ InputDevice_ResetTS,
   /* .SetAMCT = */ InputDevice_SetAMCT,
   /* .SetCrosshairsCursor = */ InputDevice_SetCrosshairsCursor,
   /* .SetCrosshairsColor = */ InputDevice_SetCrosshairsColor,
   /* .SetDTR = */ InputDevice_neGcon_SetDTR,
   /* .GetDSR = */ InputDevice_neGcon_GetDSR,
   /* .Clock = */ InputDevice_neGcon_Clock,
   /* .GetNVData = */ InputDevice_GetNVData,
   /* .GetNVSize = */ InputDevice_GetNVSize,
   /* .ReadNV = */ InputDevice_ReadNV,
   /* .WriteNV = */ InputDevice_WriteNV,
   /* .GetNVDirtyCount = */ InputDevice_GetNVDirtyCount,
   /* .ResetNVDirtyCount = */ InputDevice_ResetNVDirtyCount,
   /* .Destroy = */ NULL,
};

static const InputDevice_VTable InputDevice_neGconRumble_vtable = {
   /* .Power = */ InputDevice_neGconRumble_Power,
   /* .UpdateInput = */ InputDevice_neGconRumble_UpdateInput,
   /* .StateAction = */ InputDevice_neGconRumble_StateAction,
   /* .GPULineHook = */ InputDevice_GPULineHook,
   /* .Update = */ InputDevice_neGconRumble_Update,
   /* .ResetTS = */ InputDevice_neGconRumble_ResetTS,
   /* .SetAMCT = */ InputDevice_neGconRumble_SetAMCT,
   /* .SetCrosshairsCursor = */ InputDevice_SetCrosshairsCursor,
   /* .SetCrosshairsColor = */ InputDevice_SetCrosshairsColor,
   /* .SetDTR = */ InputDevice_neGconRumble_SetDTR,
   /* .GetDSR = */ InputDevice_neGconRumble_GetDSR,
   /* .Clock = */ InputDevice_neGconRumble_Clock,
   /* .GetNVData = */ InputDevice_GetNVData,
   /* .GetNVSize = */ InputDevice_GetNVSize,
   /* .ReadNV = */ InputDevice_ReadNV,
   /* .WriteNV = */ InputDevice_WriteNV,
   /* .GetNVDirtyCount = */ InputDevice_GetNVDirtyCount,
   /* .ResetNVDirtyCount = */ InputDevice_ResetNVDirtyCount,
   /* .Destroy = */ NULL,
};

static const InputDevice_VTable InputDevice_GunCon_vtable = {
   /* .Power = */ InputDevice_GunCon_Power,
   /* .UpdateInput = */ InputDevice_GunCon_UpdateInput,
   /* .StateAction = */ InputDevice_GunCon_StateAction,
   /* .GPULineHook = */ InputDevice_GunCon_GPULineHook,
   /* .Update = */ InputDevice_Update,
   /* .ResetTS = */ InputDevice_ResetTS,
   /* .SetAMCT = */ InputDevice_SetAMCT,
   /* .SetCrosshairsCursor = */ InputDevice_SetCrosshairsCursor,
   /* .SetCrosshairsColor = */ InputDevice_SetCrosshairsColor,
   /* .SetDTR = */ InputDevice_GunCon_SetDTR,
   /* .GetDSR = */ InputDevice_GunCon_GetDSR,
   /* .Clock = */ InputDevice_GunCon_Clock,
   /* .GetNVData = */ InputDevice_GetNVData,
   /* .GetNVSize = */ InputDevice_GetNVSize,
   /* .ReadNV = */ InputDevice_ReadNV,
   /* .WriteNV = */ InputDevice_WriteNV,
   /* .GetNVDirtyCount = */ InputDevice_GetNVDirtyCount,
   /* .ResetNVDirtyCount = */ InputDevice_ResetNVDirtyCount,
   /* .Destroy = */ NULL,
};

static const InputDevice_VTable InputDevice_Justifier_vtable = {
   /* .Power = */ InputDevice_Justifier_Power,
   /* .UpdateInput = */ InputDevice_Justifier_UpdateInput,
   /* .StateAction = */ InputDevice_Justifier_StateAction,
   /* .GPULineHook = */ InputDevice_Justifier_GPULineHook,
   /* .Update = */ InputDevice_Update,
   /* .ResetTS = */ InputDevice_ResetTS,
   /* .SetAMCT = */ InputDevice_SetAMCT,
   /* .SetCrosshairsCursor = */ InputDevice_SetCrosshairsCursor,
   /* .SetCrosshairsColor = */ InputDevice_SetCrosshairsColor,
   /* .SetDTR = */ InputDevice_Justifier_SetDTR,
   /* .GetDSR = */ InputDevice_Justifier_GetDSR,
   /* .Clock = */ InputDevice_Justifier_Clock,
   /* .GetNVData = */ InputDevice_GetNVData,
   /* .GetNVSize = */ InputDevice_GetNVSize,
   /* .ReadNV = */ InputDevice_ReadNV,
   /* .WriteNV = */ InputDevice_WriteNV,
   /* .GetNVDirtyCount = */ InputDevice_GetNVDirtyCount,
   /* .ResetNVDirtyCount = */ InputDevice_ResetNVDirtyCount,
   /* .Destroy = */ NULL,
};

static const InputDevice_VTable InputDevice_Memcard_vtable = {
   /* .Power = */ InputDevice_Memcard_Power,
   /* .UpdateInput = */ InputDevice_UpdateInput,
   /* .StateAction = */ InputDevice_Memcard_StateAction,
   /* .GPULineHook = */ InputDevice_GPULineHook,
   /* .Update = */ InputDevice_Update,
   /* .ResetTS = */ InputDevice_ResetTS,
   /* .SetAMCT = */ InputDevice_SetAMCT,
   /* .SetCrosshairsCursor = */ InputDevice_SetCrosshairsCursor,
   /* .SetCrosshairsColor = */ InputDevice_SetCrosshairsColor,
   /* .SetDTR = */ InputDevice_Memcard_SetDTR,
   /* .GetDSR = */ InputDevice_Memcard_GetDSR,
   /* .Clock = */ InputDevice_Memcard_Clock,
   /* .GetNVData = */ InputDevice_Memcard_GetNVData,
   /* .GetNVSize = */ InputDevice_Memcard_GetNVSize,
   /* .ReadNV = */ InputDevice_Memcard_ReadNV,
   /* .WriteNV = */ InputDevice_Memcard_WriteNV,
   /* .GetNVDirtyCount = */ InputDevice_Memcard_GetNVDirtyCount,
   /* .ResetNVDirtyCount = */ InputDevice_Memcard_ResetNVDirtyCount,
   /* .Destroy = */ NULL,
};

static const InputDevice_VTable InputDevice_Multitap_vtable = {
   /* .Power = */ InputDevice_Multitap_Power,
   /* .UpdateInput = */ InputDevice_UpdateInput,
   /* .StateAction = */ InputDevice_Multitap_StateAction,
   /* .GPULineHook = */ InputDevice_GPULineHook,
   /* .Update = */ InputDevice_Update,
   /* .ResetTS = */ InputDevice_ResetTS,
   /* .SetAMCT = */ InputDevice_SetAMCT,
   /* .SetCrosshairsCursor = */ InputDevice_SetCrosshairsCursor,
   /* .SetCrosshairsColor = */ InputDevice_SetCrosshairsColor,
   /* .SetDTR = */ InputDevice_Multitap_SetDTR,
   /* .GetDSR = */ InputDevice_Multitap_GetDSR,
   /* .Clock = */ InputDevice_Multitap_Clock,
   /* .GetNVData = */ InputDevice_GetNVData,
   /* .GetNVSize = */ InputDevice_GetNVSize,
   /* .ReadNV = */ InputDevice_ReadNV,
   /* .WriteNV = */ InputDevice_WriteNV,
   /* .GetNVDirtyCount = */ InputDevice_GetNVDirtyCount,
   /* .ResetNVDirtyCount = */ InputDevice_ResetNVDirtyCount,
   /* .Destroy = */ NULL,
};

static const InputDevice_VTable InputDevice_default_vtable = {
   /* .Power = */ InputDevice_Power,
   /* .UpdateInput = */ InputDevice_UpdateInput,
   /* .StateAction = */ InputDevice_StateAction,
   /* .GPULineHook = */ InputDevice_GPULineHook,
   /* .Update = */ InputDevice_Update,
   /* .ResetTS = */ InputDevice_ResetTS,
   /* .SetAMCT = */ InputDevice_SetAMCT,
   /* .SetCrosshairsCursor = */ InputDevice_SetCrosshairsCursor,
   /* .SetCrosshairsColor = */ InputDevice_SetCrosshairsColor,
   /* .SetDTR = */ InputDevice_SetDTR,
   /* .GetDSR = */ InputDevice_GetDSR,
   /* .Clock = */ InputDevice_Clock,
   /* .GetNVData = */ InputDevice_GetNVData,
   /* .GetNVSize = */ InputDevice_GetNVSize,
   /* .ReadNV = */ InputDevice_ReadNV,
   /* .WriteNV = */ InputDevice_WriteNV,
   /* .GetNVDirtyCount = */ InputDevice_GetNVDirtyCount,
   /* .ResetNVDirtyCount = */ InputDevice_ResetNVDirtyCount,
   /* .Destroy = */ NULL,
};


static InputDevice *InputDevice_New(void)
{
   InputDevice *dev = (InputDevice *)calloc(1, sizeof(*dev));
   if (!dev) return NULL;
   dev->vt = &InputDevice_default_vtable;
   dev->chair_r = 0;
   dev->chair_g = 0;
   dev->chair_b = 0;
   dev->chair_cursor = SETTING_GUN_CROSSHAIR_CROSS;
   dev->chair_x = -1000;
   dev->chair_y = -1000;
   return dev;
}

InputDevice *Device_Gamepad_Create(void)
{
   InputDevice_Gamepad *dev = (InputDevice_Gamepad *)calloc(1, sizeof(*dev));
   if (!dev) return NULL;
   dev->base.vt = &InputDevice_Gamepad_vtable;
   dev->base.chair_r = 0;
   dev->base.chair_g = 0;
   dev->base.chair_b = 0;
   dev->base.chair_cursor = SETTING_GUN_CROSSHAIR_CROSS;
   dev->base.chair_x = -1000;
   dev->base.chair_y = -1000;
   InputDevice_Gamepad_Ctor(&dev->base);
   return &dev->base;
}

InputDevice *Device_DualAnalog_Create(bool joystick_mode)
{
   InputDevice_DualAnalog *dev = (InputDevice_DualAnalog *)calloc(1, sizeof(*dev));
   if (!dev) return NULL;
   dev->base.vt = &InputDevice_DualAnalog_vtable;
   dev->base.chair_r = 0;
   dev->base.chair_g = 0;
   dev->base.chair_b = 0;
   dev->base.chair_cursor = SETTING_GUN_CROSSHAIR_CROSS;
   dev->base.chair_x = -1000;
   dev->base.chair_y = -1000;
   InputDevice_DualAnalog_Ctor(&dev->base, joystick_mode);
   return &dev->base;
}

InputDevice *Device_DualShock_Create(const char *name)
{
   InputDevice_DualShock *dev = (InputDevice_DualShock *)calloc(1, sizeof(*dev));
   if (!dev) return NULL;
   dev->base.vt = &InputDevice_DualShock_vtable;
   dev->base.chair_r = 0;
   dev->base.chair_g = 0;
   dev->base.chair_b = 0;
   dev->base.chair_cursor = SETTING_GUN_CROSSHAIR_CROSS;
   dev->base.chair_x = -1000;
   dev->base.chair_y = -1000;
   InputDevice_DualShock_Ctor(&dev->base, name);
   return &dev->base;
}

InputDevice *Device_Mouse_Create(void)
{
   InputDevice_Mouse *dev = (InputDevice_Mouse *)calloc(1, sizeof(*dev));
   if (!dev) return NULL;
   dev->base.vt = &InputDevice_Mouse_vtable;
   dev->base.chair_r = 0;
   dev->base.chair_g = 0;
   dev->base.chair_b = 0;
   dev->base.chair_cursor = SETTING_GUN_CROSSHAIR_CROSS;
   dev->base.chair_x = -1000;
   dev->base.chair_y = -1000;
   InputDevice_Mouse_Ctor(&dev->base);
   return &dev->base;
}

InputDevice *Device_neGcon_Create(void)
{
   InputDevice_neGcon *dev = (InputDevice_neGcon *)calloc(1, sizeof(*dev));
   if (!dev) return NULL;
   dev->base.vt = &InputDevice_neGcon_vtable;
   dev->base.chair_r = 0;
   dev->base.chair_g = 0;
   dev->base.chair_b = 0;
   dev->base.chair_cursor = SETTING_GUN_CROSSHAIR_CROSS;
   dev->base.chair_x = -1000;
   dev->base.chair_y = -1000;
   InputDevice_neGcon_Ctor(&dev->base);
   return &dev->base;
}

InputDevice *Device_neGconRumble_Create(const char *name)
{
   InputDevice_neGconRumble *dev = (InputDevice_neGconRumble *)calloc(1, sizeof(*dev));
   if (!dev) return NULL;
   dev->base.vt = &InputDevice_neGconRumble_vtable;
   dev->base.chair_r = 0;
   dev->base.chair_g = 0;
   dev->base.chair_b = 0;
   dev->base.chair_cursor = SETTING_GUN_CROSSHAIR_CROSS;
   dev->base.chair_x = -1000;
   dev->base.chair_y = -1000;
   InputDevice_neGconRumble_Ctor(&dev->base, name);
   return &dev->base;
}

InputDevice *Device_GunCon_Create(void)
{
   InputDevice_GunCon *dev = (InputDevice_GunCon *)calloc(1, sizeof(*dev));
   if (!dev) return NULL;
   dev->base.vt = &InputDevice_GunCon_vtable;
   dev->base.chair_r = 0;
   dev->base.chair_g = 0;
   dev->base.chair_b = 0;
   dev->base.chair_cursor = SETTING_GUN_CROSSHAIR_CROSS;
   dev->base.chair_x = -1000;
   dev->base.chair_y = -1000;
   InputDevice_GunCon_Ctor(&dev->base);
   return &dev->base;
}

InputDevice *Device_Justifier_Create(void)
{
   InputDevice_Justifier *dev = (InputDevice_Justifier *)calloc(1, sizeof(*dev));
   if (!dev) return NULL;
   dev->base.vt = &InputDevice_Justifier_vtable;
   dev->base.chair_r = 0;
   dev->base.chair_g = 0;
   dev->base.chair_b = 0;
   dev->base.chair_cursor = SETTING_GUN_CROSSHAIR_CROSS;
   dev->base.chair_x = -1000;
   dev->base.chair_y = -1000;
   InputDevice_Justifier_Ctor(&dev->base);
   return &dev->base;
}

InputDevice *Device_Memcard_Create(void)
{
   InputDevice_Memcard *dev = (InputDevice_Memcard *)calloc(1, sizeof(*dev));
   if (!dev) return NULL;
   dev->base.vt = &InputDevice_Memcard_vtable;
   dev->base.chair_r = 0;
   dev->base.chair_g = 0;
   dev->base.chair_b = 0;
   dev->base.chair_cursor = SETTING_GUN_CROSSHAIR_CROSS;
   dev->base.chair_x = -1000;
   dev->base.chair_y = -1000;
   InputDevice_Memcard_Ctor(&dev->base);
   return &dev->base;
}

InputDevice *Device_Multitap_Create(void)
{
   InputDevice_Multitap *dev = (InputDevice_Multitap *)calloc(1, sizeof(*dev));
   if (!dev) return NULL;
   dev->base.vt = &InputDevice_Multitap_vtable;
   dev->base.chair_r = 0;
   dev->base.chair_g = 0;
   dev->base.chair_b = 0;
   dev->base.chair_cursor = SETTING_GUN_CROSSHAIR_CROSS;
   dev->base.chair_x = -1000;
   dev->base.chair_y = -1000;
   InputDevice_Multitap_Ctor(&dev->base);
   return &dev->base;
}


FrontIO *FrontIO_New(bool emulate_memcards[8], bool emulate_multitap[2])
{
   FrontIO *fio = (FrontIO *)calloc(1, sizeof(*fio));
   if (!fio) return NULL;
   FrontIO_Ctor(fio, emulate_memcards, emulate_multitap);
   return fio;
}

void FrontIO_Free(FrontIO *fio)
{
   if (!fio) return;
   FrontIO_Destroy(fio);
   free(fio);
}
