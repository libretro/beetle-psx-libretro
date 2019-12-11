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

#include "psx.h"
#include "frontio.h"
#include <compat/msvc.h>

#include "../state_helpers.h"
#include "../video/surface.h"

#include "input/gamepad.h"
#include "input/dualanalog.h"
#include "input/dualshock.h"
#include "input/mouse.h"
#include "input/negcon.h"
#include "input/guncon.h"
#include "input/justifier.h"

#include "input/memcard.h"

#include "input/multitap.h"

//#define PSX_FIODBGINFO(format, ...) { /* printf(format " -- timestamp=%d -- PAD temp\n", ## __VA_ARGS__, timestamp); */  }
static void PSX_FIODBGINFO(const char *format, ...)
{
}

InputDevice::InputDevice() :
	chair_r(0),
	chair_g(0),
	chair_b(0),
	chair_cursor( FrontIO::SETTING_GUN_CROSSHAIR_CROSS ),
	chair_x(-1000),
	chair_y(-1000)
{
}

InputDevice::~InputDevice()
{
}

void InputDevice::Power(void)
{
}

int InputDevice::StateAction(StateMem* sm, int load, int data_only, const char* section_name)
{
 return(1);
}

void InputDevice::Update(const int32_t timestamp)
{

}

void InputDevice::ResetTS(void)
{

}

void InputDevice::SetAMCT(bool)
{

}

void InputDevice::SetCrosshairsCursor(int cursor)
{
	if ( cursor >= 0 && cursor < FrontIO::SETTING_GUN_CROSSHAIR_LAST ) {
		chair_cursor = cursor;
	}
}

void InputDevice::SetCrosshairsColor(uint32_t color)
{
   chair_r = (color >> 16) & 0xFF;
   chair_g = (color >>  8) & 0xFF;
   chair_b = (color >>  0) & 0xFF;
}

static void crosshair_plot( uint32 *pixels,
							int x,
							const MDFN_PixelFormat* const format,
							unsigned chair_r,
							unsigned chair_g,
							unsigned chair_b )
{
	int r, g, b, a;
	int nr, ng, nb;

	format->DecodeColor(pixels[x], r, g, b, a);

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

INLINE void InputDevice::DrawCrosshairs(uint32 *pixels, const MDFN_PixelFormat* const format, const unsigned width, const unsigned pix_clock, const unsigned surf_pitchinpix, const unsigned upscale_factor)
{
	switch ( chair_cursor )
	{

	case FrontIO::SETTING_GUN_CROSSHAIR_OFF:
		return;

	case FrontIO::SETTING_GUN_CROSSHAIR_CROSS:

		if ( chair_y >= -8 && chair_y <= 8 )
		{
			int32 ic;
			int32 x_start, x_bound;

			if ( chair_y == 0 ) {
				ic = pix_clock / 762925;
			} else {
				ic = 0;
			}

			x_start = std::max<int32>(0, (chair_x - ic) * upscale_factor);
			x_bound = std::min<int32>(width * upscale_factor, (chair_x + ic + 1) * upscale_factor);

			for ( int32 x = x_start; x < x_bound; x++ )
			{
            for (int row = 0; row < upscale_factor; row++)
            {
               crosshair_plot( pixels, x + (row * surf_pitchinpix), format, chair_r, chair_g, chair_b );
            }
			}
		}

		break;

	case FrontIO::SETTING_GUN_CROSSHAIR_DOT:

		if ( chair_y >= -1 && chair_y <= 1 )
		{
			int32 ic;
			int32 x_start, x_bound;

			ic = pix_clock / ( 762925 * 6 );

			x_start = std::max<int32>(0, (chair_x - ic) * upscale_factor);
			x_bound = std::min<int32>(width * upscale_factor, (chair_x + ic) * upscale_factor);

			for ( int32 x = x_start; x < x_bound; x++ )
			{
            for (int row = 0; row < upscale_factor; row++)
            {
               crosshair_plot( pixels, x + (row * surf_pitchinpix), format, chair_r, chair_g, chair_b );
            }
			}
		}

		break;

	}; // switch ( chair_cursor )
}

int FrontIO::StateAction(StateMem* sm, int load, int data_only)
{
 SFORMAT StateRegs[] =
 {
  SFVAR(ClockDivider),

  SFVAR(ReceivePending),
  SFVAR(TransmitPending),

  SFVAR(ReceiveInProgress),
  SFVAR(TransmitInProgress),

  SFVAR(ReceiveBufferAvail),

  SFVAR(ReceiveBuffer),
  SFVAR(TransmitBuffer),

  SFVAR(ReceiveBitCounter),
  SFVAR(TransmitBitCounter),

  SFVAR(Mode),
  SFVAR(Control),
  SFVAR(Baudrate),

  SFVAR(istatus),

  // FIXME: Step mode save states.
  SFARRAY32(irq10_pulse_ts, sizeof(irq10_pulse_ts) / sizeof(irq10_pulse_ts[0])),
  SFARRAY32(dsr_pulse_delay, sizeof(dsr_pulse_delay) / sizeof(dsr_pulse_delay[0])),
  SFARRAY32(dsr_active_until_ts, sizeof(dsr_active_until_ts) / sizeof(dsr_active_until_ts[0])),

  SFEND
 };

 int ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, "FIO");

 for(unsigned i = 0; i < 8; i++)
 {
  char tmpbuf[32];
  snprintf(tmpbuf, sizeof(tmpbuf), "FIODEV%u", i);

  ret &= Devices[i]->StateAction(sm, load, data_only, tmpbuf);
 }

 for(unsigned i = 0; i < 8; i++)
 {
  char tmpbuf[32];
  snprintf(tmpbuf, sizeof(tmpbuf), "FIOMC%u", i);

  ret &= DevicesMC[i]->StateAction(sm, load, data_only, tmpbuf);
 }

 for(unsigned i = 0; i < 2; i++)
 {
  char tmpbuf[32];
  snprintf(tmpbuf, sizeof(tmpbuf), "FIOTAP%u", i);

  ret &= DevicesTap[i]->StateAction(sm, load, data_only, tmpbuf);
 }

 if(load)
 {
    ::IRQ_Assert(IRQ_SIO, istatus);
 }

 return(ret);
}

bool InputDevice::RequireNoFrameskip(void)
{
 return false;
}

int32_t InputDevice::GPULineHook(const int32_t timestamp, bool vsync, uint32 *pixels, const MDFN_PixelFormat* const format, const unsigned width, const unsigned pix_clock_offset, const unsigned pix_clock, const unsigned pix_clock_divider, const unsigned surf_pitchinpix, const unsigned upscale_factor)
{
 return(PSX_EVENT_MAXTS);
}


void InputDevice::UpdateInput(const void *data)
{
}


void InputDevice::SetDTR(bool new_dtr)
{

}

bool InputDevice::GetDSR(void)
{
   return 0;
}

bool InputDevice::Clock(bool TxD, int32_t &dsr_pulse_delay)
{
   dsr_pulse_delay = 0;

   return 1;
}

uint32_t InputDevice::GetNVSize(void)
{
   return 0;
}

void InputDevice::ReadNV(uint8_t *buffer, uint32_t offset, uint32_t count)
{

}

void InputDevice::WriteNV(const uint8_t *buffer, uint32_t offset, uint32_t count)
{

}

uint64_t InputDevice::GetNVDirtyCount(void)
{
   return 0;
}

void InputDevice::ResetNVDirtyCount(void)
{

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

InputDevice *FrontIO::GetMemcardDevice(unsigned int which)
{
   if (DevicesMC[which])
      return DevicesMC[which];
   return NULL;
}

void FrontIO::MapDevicesToPorts(void)
{
   int i;
   if(emulate_multitap[0] && emulate_multitap[1])
   {
      for (i = 0; i < 2; i++)
      {
         Ports[i] = DevicesTap[i];
         MCPorts[i] = DummyDevice;
      }
   }
   else if(!emulate_multitap[0] && emulate_multitap[1])
   {
      Ports[0] = Devices[0];
      MCPorts[0] = emulate_memcards[0] ? DevicesMC[0] : DummyDevice;

      Ports[1] = DevicesTap[1];
      MCPorts[1] = DummyDevice;
   }
   else if(emulate_multitap[0] && !emulate_multitap[1])
   {
      Ports[0] = DevicesTap[0];
      MCPorts[0] = DummyDevice;

      Ports[1] = Devices[4];
      MCPorts[1] = emulate_memcards[4] ? DevicesMC[4] : DummyDevice;
   }
   else
   {
      for(i = 0; i < 2; i++)
      {
         Ports[i] = Devices[i];
         MCPorts[i] = emulate_memcards[i] ? DevicesMC[i] : DummyDevice;
      }
   }

   //printf("\n");
   for(i = 0; i < 8; i++)
   {
      unsigned mp = EP_to_MP(emulate_multitap, i);

      if(emulate_multitap[mp])
         DevicesTap[mp]->SetSubDevice(EP_to_SP(emulate_multitap, i), Devices[i], emulate_memcards[i] ? DevicesMC[i] : DummyDevice);
      else
         DevicesTap[mp]->SetSubDevice(EP_to_SP(emulate_multitap, i), DummyDevice, DummyDevice);

      //printf("%d-> multitap: %d, sub-port: %d\n", i, mp, EP_to_SP(emulate_multitap, i));
   }
}

FrontIO::FrontIO(bool emulate_memcards_[8], bool emulate_multitap_[2])
{
   int i;
   memcpy(emulate_memcards, emulate_memcards_, sizeof(emulate_memcards));
   memcpy(emulate_multitap, emulate_multitap_, sizeof(emulate_multitap));

   DummyDevice = new InputDevice();

   for(i = 0; i < 8; i++)
   {
      DeviceData[i] = NULL;
      Devices[i] = new InputDevice();
      DevicesMC[i] = Device_Memcard_Create();
      chair_cursor[i] = SETTING_GUN_CROSSHAIR_CROSS;
      Devices[i]->SetCrosshairsCursor(chair_cursor[i]);
      chair_colors[i] = 1 << 24;
      Devices[i]->SetCrosshairsColor(chair_colors[i]);
   }

   for(i = 0; i < 2; i++)
      DevicesTap[i] = new InputDevice_Multitap();

   MapDevicesToPorts();
}

void FrontIO::SetAMCT(bool enabled)
{
   int i;
   for(i = 0; i < 8; i++)
      Devices[i]->SetAMCT(enabled);
   amct_enabled = enabled;
}

void FrontIO::SetCrosshairsCursor(unsigned port, int cursor)
{
   chair_cursor[port] = cursor;
   Devices[port]->SetCrosshairsCursor(cursor);
}

void FrontIO::SetCrosshairsColor(unsigned port, uint32_t color)
{
   chair_colors[port] = color;
   Devices[port]->SetCrosshairsColor(color);
}

FrontIO::~FrontIO()
{
   int i;
   for(i = 0; i < 8; i++)
   {
      if(Devices[i])
         delete Devices[i];
      Devices[i] = NULL;
      if(DevicesMC[i])
         delete DevicesMC[i];
      DevicesMC[i] = NULL;
   }

   for(i = 0; i < 2; i++)
   {
      if(DevicesTap[i])
         delete DevicesTap[i];
      DevicesTap[i] = NULL;
   }

   if(DummyDevice)
      delete DummyDevice;
   DummyDevice = NULL;
}

int32_t FrontIO::CalcNextEventTS(int32_t timestamp, int32_t next_event)
{
   int32_t ret;
   int i;

   if(ClockDivider > 0 && ClockDivider < next_event)
      next_event = ClockDivider;

   for(i = 0; i < 4; i++)
      if(dsr_pulse_delay[i] > 0 && next_event > dsr_pulse_delay[i])
         next_event = dsr_pulse_delay[i];

   overclock_device_to_cpu(next_event);

   ret = timestamp + next_event;

   /* XXX Not sure if this is overclock-proof. This is probably only
      useful for lightgun support however */
   if(irq10_pulse_ts[0] < ret)
      ret = irq10_pulse_ts[0];

   if(irq10_pulse_ts[1] < ret)
      ret = irq10_pulse_ts[1];

   return(ret);
}

static const uint8_t ScaleShift[4] = { 0, 0, 4, 6 };

void FrontIO::CheckStartStopPending(int32_t timestamp, bool skip_event_set)
{
   //const bool prior_ReceiveInProgress = ReceiveInProgress;
   //const bool prior_TransmitInProgress = TransmitInProgress;
   bool trigger_condition = false;

   trigger_condition = (ReceivePending && (Control & 0x4)) || (TransmitPending && (Control & 0x1));

   if(trigger_condition)
   {
      if(ReceivePending)
      {
         ReceivePending = false;
         ReceiveInProgress = true;
         ReceiveBufferAvail = false;
         ReceiveBuffer = 0;
         ReceiveBitCounter = 0;
      }

      if(TransmitPending)
      {
         TransmitPending = false;
         TransmitInProgress = true;
         TransmitBitCounter = 0;
      }

      ClockDivider = std::max<uint32>(0x20, (Baudrate << ScaleShift[Mode & 0x3]) & ~1); // Minimum of 0x20 is an emulation sanity check to prevent severe performance degradation.
      //printf("CD: 0x%02x\n", ClockDivider);
   }

   if(!(Control & 0x5))
   {
      ReceiveInProgress = false;
      TransmitInProgress = false;
   }

   if(!ReceiveInProgress && !TransmitInProgress)
      ClockDivider = 0;

   if(!(skip_event_set))
      PSX_SetEventNT(PSX_EVENT_FIO, CalcNextEventTS(timestamp, 0x10000000));
}

// DSR IRQ bit setting appears(from indirect tests on real PS1) to be level-sensitive, not edge-sensitive
INLINE void FrontIO::DoDSRIRQ(void)
{
   if(Control & 0x1000)
   {
      PSX_FIODBGINFO("[DSR] IRQ");
      istatus = true;
      ::IRQ_Assert(IRQ_SIO, true);
   }
}


void FrontIO::Write(int32_t timestamp, uint32_t A, uint32_t V)
{
   assert(!(A & 0x1));

   PSX_FIODBGINFO("[FIO] Write: %08x %08x", A, V);

   Update(timestamp);

   switch(A & 0xF)
   {
      case 0x0:
         TransmitBuffer = V;
         TransmitPending = true;
         TransmitInProgress = false;
         break;

      case 0x8:
         Mode = V & 0x013F;
         break;

      case 0xa:
         if(ClockDivider > 0 && ((V & 0x2000) != (Control & 0x2000)) && ((Control & 0x2) == (V & 0x2))  )
            PSX_DBG(PSX_DBG_WARNING, "FIO device selection changed during comm %04x->%04x\n", Control, V);

         //printf("Control: %d, %04x\n", timestamp, V);
         Control = V & 0x3F2F;

         if(V & 0x10)
         {
            istatus = false;
            ::IRQ_Assert(IRQ_SIO, false);
         }

         if(V & 0x40)	// Reset
         {
            istatus = false;
            ::IRQ_Assert(IRQ_SIO, false);

            ClockDivider = 0;
            ReceivePending = false;
            TransmitPending = false;

            ReceiveInProgress = false;
            TransmitInProgress = false;

            ReceiveBufferAvail = false;

            TransmitBuffer = 0;
            ReceiveBuffer = 0;

            ReceiveBitCounter = 0;
            TransmitBitCounter = 0;

            Mode = 0;
            Control = 0;
            Baudrate = 0;
         }

         Ports[0]->SetDTR((Control & 0x2) && !(Control & 0x2000));
         MCPorts[0]->SetDTR((Control & 0x2) && !(Control & 0x2000));
         Ports[1]->SetDTR((Control & 0x2) && (Control & 0x2000));
         MCPorts[1]->SetDTR((Control & 0x2) && (Control & 0x2000));

#if 1
         if(!((Control & 0x2) && !(Control & 0x2000)))
         {
            dsr_pulse_delay[0] = 0;
            dsr_pulse_delay[2] = 0;
            dsr_active_until_ts[0] = -1;
            dsr_active_until_ts[2] = -1;
         }

         if(!((Control & 0x2) && (Control & 0x2000)))
         {
            dsr_pulse_delay[1] = 0;
            dsr_pulse_delay[3] = 0;
            dsr_active_until_ts[1] = -1;
            dsr_active_until_ts[3] = -1;
         }

#endif
         // TODO: Uncomment out in the future once our CPU emulation is a bit more accurate with timing, to prevent causing problems with games
         // that may clear the IRQ in an unsafe pattern that only works because its execution was slow enough to allow DSR to go inactive.  (Whether or not
         // such games even exist though is unknown!)
         //if(timestamp < dsr_active_until_ts[0] || timestamp < dsr_active_until_ts[1] || timestamp < dsr_active_until_ts[2] || timestamp < dsr_active_until_ts[3])
         // DoDSRIRQ();

         break;

      case 0xe:
         Baudrate = V;
         //printf("%02x\n", V);
         //MDFN_DispMessage("%02x\n", V);
         break;
   }

   CheckStartStopPending(timestamp, false);
}


uint32_t FrontIO::Read(int32_t timestamp, uint32_t A)
{
   uint32_t ret = 0;

   assert(!(A & 0x1));

   Update(timestamp);

   switch(A & 0xF)
   {
      case 0x0:
         //printf("FIO Read: 0x%02x\n", ReceiveBuffer);
         ret = ReceiveBuffer | (ReceiveBuffer << 8) | (ReceiveBuffer << 16) | (ReceiveBuffer << 24);
         ReceiveBufferAvail = false;
         ReceivePending = true;
         ReceiveInProgress = false;
         CheckStartStopPending(timestamp, false);
         break;

      case 0x4:
         ret = 0;

         if(!TransmitPending && !TransmitInProgress)
            ret |= 0x1;

         if(ReceiveBufferAvail)
            ret |= 0x2;

         if(timestamp < dsr_active_until_ts[0] || timestamp < dsr_active_until_ts[1] || timestamp < dsr_active_until_ts[2] || timestamp < dsr_active_until_ts[3])
            ret |= 0x80;

         if(istatus)
            ret |= 0x200;

         break;

      case 0x8:
         ret = Mode;
         break;

      case 0xa:
         ret = Control;
         break;

      case 0xe:
         ret = Baudrate;
         break;
   }

   if((A & 0xF) != 0x4)
      PSX_FIODBGINFO("[FIO] Read: %08x %08x", A, ret);

   return(ret);
}

int32_t FrontIO::Update(int32_t timestamp)
{
   int32_t clocks, i;
   bool need_start_stop_check = false;

   clocks = timestamp - lastts;

   overclock_cpu_to_device(clocks);

   for(i = 0; i < 4; i++)
      if(dsr_pulse_delay[i] > 0)
      {
         dsr_pulse_delay[i] -= clocks;
         if(dsr_pulse_delay[i] <= 0)
         {
            int32_t off = 32 + dsr_pulse_delay[i];

            overclock_device_to_cpu(off);

            dsr_active_until_ts[i] = timestamp + off;
            DoDSRIRQ();
         }
      }

   for(i = 0; i < 2; i++)
   {
      if(timestamp >= irq10_pulse_ts[i])
      {
         //printf("Yay: %d %u\n", i, timestamp);
         irq10_pulse_ts[i] = PSX_EVENT_MAXTS;
         ::IRQ_Assert(IRQ_PIO, true);
         ::IRQ_Assert(IRQ_PIO, false);
      }
   }

   if(ClockDivider > 0)
   {
      ClockDivider -= clocks;

      while(ClockDivider <= 0)
      {
         if(ReceiveInProgress || TransmitInProgress)
         {
            bool rxd = 0, txd = 0;
            const uint32_t BCMask = 0x07;

            if(TransmitInProgress)
            {
               txd = (TransmitBuffer >> TransmitBitCounter) & 1;
               TransmitBitCounter = (TransmitBitCounter + 1) & BCMask;
               if(!TransmitBitCounter)
               {
                  need_start_stop_check = true;
                  PSX_FIODBGINFO("[FIO] Data transmitted: %08x", TransmitBuffer);
                  TransmitInProgress = false;

                  if(Control & 0x400)
                  {
                     istatus = true;
                     ::IRQ_Assert(IRQ_SIO, true);
                  }
               }
            }

            rxd = Ports[0]->Clock(txd, dsr_pulse_delay[0]) & Ports[1]->Clock(txd, dsr_pulse_delay[1]) &
               MCPorts[0]->Clock(txd, dsr_pulse_delay[2]) & MCPorts[1]->Clock(txd, dsr_pulse_delay[3]);

            if(ReceiveInProgress)
            {
               ReceiveBuffer &= ~(1 << ReceiveBitCounter);
               ReceiveBuffer |= rxd << ReceiveBitCounter;

               ReceiveBitCounter = (ReceiveBitCounter + 1) & BCMask;

               if(!ReceiveBitCounter)
               {
                  need_start_stop_check = true;
                  PSX_FIODBGINFO("[FIO] Data received: %08x", ReceiveBuffer);

                  ReceiveInProgress = false;
                  ReceiveBufferAvail = true;

                  if(Control & 0x800)
                  {
                     istatus = true;
                     ::IRQ_Assert(IRQ_SIO, true);
                  }
               }
            }
            ClockDivider += std::max<uint32>(0x20, (Baudrate << ScaleShift[Mode & 0x3]) & ~1); // Minimum of 0x20 is an emulation sanity check to prevent severe performance degradation.
         }
         else
            break;
      }
   }


   lastts = timestamp;


   if(need_start_stop_check)
   {
      CheckStartStopPending(timestamp, true);
   }

   return(CalcNextEventTS(timestamp, 0x10000000));
}

void FrontIO::ResetTS(void)
{
   int i;
   for(i = 0; i < 8; i++)
   {
      Devices[i]->Update(lastts);	// Maybe eventually call Update() from FrontIO::Update() and remove this(but would hurt speed)?
      Devices[i]->ResetTS();

      DevicesMC[i]->Update(lastts);	// Maybe eventually call Update() from FrontIO::Update() and remove this(but would hurt speed)?
      DevicesMC[i]->ResetTS();
   }

   for(i = 0; i < 2; i++)
   {
      DevicesTap[i]->Update(lastts);
      DevicesTap[i]->ResetTS();
   }

   for(i = 0; i < 2; i++)
   {
      if(irq10_pulse_ts[i] != PSX_EVENT_MAXTS)
         irq10_pulse_ts[i] -= lastts;
   }

   for(i = 0; i < 4; i++)
   {
      if(dsr_active_until_ts[i] >= 0)
      {
         dsr_active_until_ts[i] -= lastts;
         //printf("SPOOONY: %d %d\n", i, dsr_active_until_ts[i]);
      }
   }
   lastts = 0;
}


void FrontIO::Power(void)
{
   int i;
   for(i = 0; i < 4; i++)
   {
      dsr_pulse_delay[i] = 0;
      dsr_active_until_ts[i] = -1;
   }

   for(i = 0; i < 2; i++)
      irq10_pulse_ts[i] = PSX_EVENT_MAXTS;

   lastts = 0;

   //
   //

   ClockDivider = 0;

   ReceivePending = false;
   TransmitPending = false;

   ReceiveInProgress = false;
   TransmitInProgress = false;

   ReceiveBufferAvail = false;

   TransmitBuffer = 0;
   ReceiveBuffer = 0;

   ReceiveBitCounter = 0;
   TransmitBitCounter = 0;

   Mode = 0;
   Control = 0;
   Baudrate = 0;

   for(i = 0; i < 8; i++)
   {
      Devices[i]->Power();
      DevicesMC[i]->Power();
   }

   istatus = false;
}

void FrontIO::UpdateInput(void)
{
   int i;
   for(i = 0; i < 8; i++)
      Devices[i]->UpdateInput(DeviceData[i]);
}

void FrontIO::SetInput(unsigned int port, const char *type, void *ptr)
{
   delete Devices[port];
   Devices[port] = NULL;

   if(port < 2)
      irq10_pulse_ts[port] = PSX_EVENT_MAXTS;

   if(!strcmp(type, "gamepad") || !strcmp(type, "dancepad"))
      Devices[port] = Device_Gamepad_Create();
   else if(!strcmp(type, "dualanalog"))
      Devices[port] = Device_DualAnalog_Create(false);
   else if(!strcmp(type, "analogjoy"))
      Devices[port] = Device_DualAnalog_Create(true);
   else if(!strcmp(type, "dualshock"))
   {
      char name[256];
      snprintf(name, 256, _("DualShock on port %u"), port + 1);
      Devices[port] = Device_DualShock_Create(std::string(name));
   }
   else if(!strcmp(type, "mouse"))
      Devices[port] = Device_Mouse_Create();
   else if(!strcmp(type, "negcon"))
      Devices[port] = Device_neGcon_Create();
   else if(!strcmp(type, "guncon"))
      Devices[port] = Device_GunCon_Create();
   else if(!strcmp(type, "justifier"))
      Devices[port] = Device_Justifier_Create();
   else
      Devices[port] = new InputDevice();

   Devices[port]->SetAMCT(amct_enabled);
   Devices[port]->SetCrosshairsCursor(chair_cursor[port]);
   Devices[port]->SetCrosshairsColor(chair_colors[port]);
   DeviceData[port] = ptr;

   MapDevicesToPorts();
}

uint64_t FrontIO::GetMemcardDirtyCount(unsigned int which)
{
 assert(which < 8);

 return(DevicesMC[which]->GetNVDirtyCount());
}

void FrontIO::LoadMemcard(unsigned int which)
{
   assert(which < 8);

   if(DevicesMC[which]->GetNVSize())
   {
      DevicesMC[which]->WriteNV(DevicesMC[which]->GetNVData(), 0, (1 << 17));
      DevicesMC[which]->ResetNVDirtyCount();		// There's no need to rewrite the file if it's the same data.
   }
}

void FrontIO::LoadMemcard(unsigned int which, const char *path)
{
 assert(which < 8);

 if(DevicesMC[which]->GetNVSize())
 {
    RFILE *mf = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ, 
          RETRO_VFS_FILE_ACCESS_HINT_NONE);

    if (!mf)
       return;

    filestream_read(mf, DevicesMC[which]->GetNVData(), (1 << 17));

    DevicesMC[which]->WriteNV(DevicesMC[which]->GetNVData(), 0, (1 << 17));
    DevicesMC[which]->ResetNVDirtyCount();		// There's no need to rewrite the file if it's the same data.

    filestream_close(mf);
 }
}

void FrontIO::SaveMemcard(unsigned int which)
{
 assert(which < 8);

 if(DevicesMC[which]->GetNVSize() && DevicesMC[which]->GetNVDirtyCount())
 {
  DevicesMC[which]->ReadNV(DevicesMC[which]->GetNVData(), 0, (1 << 17));
  DevicesMC[which]->ResetNVDirtyCount();
 }
}

void FrontIO::SaveMemcard(unsigned int which, const char *path)
{
 assert(which < 8);

 if(DevicesMC[which]->GetNVSize() && DevicesMC[which]->GetNVDirtyCount())
 {
    RFILE *mf = filestream_open(path, 
          RETRO_VFS_FILE_ACCESS_WRITE,
          RETRO_VFS_FILE_ACCESS_HINT_NONE);

    if (!mf)
       return;

    DevicesMC[which]->ReadNV(DevicesMC[which]->GetNVData(), 0, (1 << 17));
    filestream_write(mf, DevicesMC[which]->GetNVData(), (1 << 17));

    filestream_close(mf);	// Call before resetting the NV dirty count!

    DevicesMC[which]->ResetNVDirtyCount();
 }
}

bool FrontIO::RequireNoFrameskip(void)
{
   unsigned i;

   for(i = 0; i < 8; i++)
      if(Devices[i]->RequireNoFrameskip())
         return(true);

   return(false);
}

void FrontIO::GPULineHook(const int32_t timestamp, const int32_t line_timestamp, bool vsync, uint32 *pixels, const MDFN_PixelFormat* const format, const unsigned width, const unsigned pix_clock_offset, const unsigned pix_clock, const unsigned pix_clock_divider, const unsigned surf_pitchinpix, const unsigned upscale_factor)
{
   Update(timestamp);

   for(unsigned i = 0; i < 8; i++)
   {
      int32_t plts = Devices[i]->GPULineHook(line_timestamp, vsync, pixels, format, width, pix_clock_offset, pix_clock, pix_clock_divider, surf_pitchinpix, upscale_factor);

      if(i < 2)
      {
         irq10_pulse_ts[i] = plts;

         if(irq10_pulse_ts[i] <= timestamp)
         {
            irq10_pulse_ts[i] = PSX_EVENT_MAXTS;
            ::IRQ_Assert(IRQ_PIO, true);
            ::IRQ_Assert(IRQ_PIO, false);
         }
      }
   }

   //
   // Draw crosshairs in a separate pass so the crosshairs won't mess up the color evaluation of later lightun GPULineHook()s.
   //
   if(pixels && pix_clock)
   {
      for(unsigned i = 0; i < 8; i++)
      {
         Devices[i]->DrawCrosshairs(pixels, format, width, pix_clock, surf_pitchinpix, upscale_factor);
      }
   }

   PSX_SetEventNT(PSX_EVENT_FIO, CalcNextEventTS(timestamp, 0x10000000));
}

static InputDeviceInfoStruct InputDeviceInfoPSXPort[] =
{
 // None
 {
  "none",
  "none",
  NULL,
  NULL,
  0,
  NULL
 },

 // Gamepad(SCPH-1080)
 {
  "gamepad",
  "Digital Gamepad",
  "PlayStation digital gamepad; SCPH-1080.",
  NULL,
  sizeof(Device_Gamepad_IDII) / sizeof(InputDeviceInputInfoStruct),
  Device_Gamepad_IDII,
 },

 // Dual Shock Gamepad(SCPH-1200)
 {
  "dualshock",
  "DualShock",
  "DualShock gamepad; SCPH-1200.  Emulation in Mednafen includes the analog mode toggle button.  Rumble is emulated, but currently only supported on Linux, and MS Windows via the XInput API and XInput-compatible gamepads/joysticks.  If you're having trouble getting rumble to work on Linux, see if Mednafen is printing out error messages during startup regarding /dev/input/event*, and resolve the issue(s) as necessary.",
  NULL,
  sizeof(Device_DualShock_IDII) / sizeof(InputDeviceInputInfoStruct),
  Device_DualShock_IDII,
 },

 // Dual Analog Gamepad(SCPH-1180), forced to analog mode.
 {
  "dualanalog",
  "Dual Analog",
  "Dual Analog gamepad; SCPH-1180.  It is the predecessor/prototype to the more advanced DualShock.  Emulated in Mednafen as forced to analog mode, and without rumble.",
  NULL,
  sizeof(Device_DualAnalog_IDII) / sizeof(InputDeviceInputInfoStruct),
  Device_DualAnalog_IDII,
 },


 // Analog joystick(SCPH-1110), forced to analog mode - emulated through a tweak to dual analog gamepad emulation.
 {
  "analogjoy",
  "Analog Joystick",
  "Flight-game-oriented dual-joystick controller; SCPH-1110.   Emulated in Mednafen as forced to analog mode.",
  NULL,
  sizeof(Device_AnalogJoy_IDII) / sizeof(InputDeviceInputInfoStruct),
  Device_AnalogJoy_IDII,
 },

 {
  "mouse",
  "Mouse",
  NULL,
  NULL,
  sizeof(Device_Mouse_IDII) / sizeof(InputDeviceInputInfoStruct),
  Device_Mouse_IDII,
 },

 {
  "negcon",
  "neGcon",
  "Namco's unconventional twisty racing-game-oriented gamepad; NPC-101.",
  NULL,
  sizeof(Device_neGcon_IDII) / sizeof(InputDeviceInputInfoStruct),
  Device_neGcon_IDII,
 },

 {
  "guncon",
  "GunCon",
  "Namco's light gun; NPC-103.",
  NULL,
  sizeof(Device_GunCon_IDII) / sizeof(InputDeviceInputInfoStruct),
  Device_GunCon_IDII,
 },

 {
  "justifier",
  "Konami Justifier",
  "Konami's light gun; SLUH-00017.  Rumored to be wrought of the coagulated rage of all who tried to shoot The Dog.  If the game you want to play supports the \"GunCon\", you should use that instead. NOTE: Currently does not work properly when on any of ports 1B-1D and 2B-2D.",
  NULL,
  sizeof(Device_Justifier_IDII) / sizeof(InputDeviceInputInfoStruct),
  Device_Justifier_IDII,
 },

 {
  "dancepad",
  "Dance Pad",
  "Dingo Dingo Rodeo!",
  NULL,
  sizeof(Device_Dancepad_IDII) / sizeof(InputDeviceInputInfoStruct),
  Device_Dancepad_IDII,
 },

};

static const InputPortInfoStruct PortInfo[] =
{
 { "port1", "Virtual Port 1", sizeof(InputDeviceInfoPSXPort) / sizeof(InputDeviceInfoStruct), InputDeviceInfoPSXPort, "gamepad" },
 { "port2", "Virtual Port 2", sizeof(InputDeviceInfoPSXPort) / sizeof(InputDeviceInfoStruct), InputDeviceInfoPSXPort, "gamepad" },
 { "port3", "Virtual Port 3", sizeof(InputDeviceInfoPSXPort) / sizeof(InputDeviceInfoStruct), InputDeviceInfoPSXPort, "gamepad" },
 { "port4", "Virtual Port 4", sizeof(InputDeviceInfoPSXPort) / sizeof(InputDeviceInfoStruct), InputDeviceInfoPSXPort, "gamepad" },
 { "port5", "Virtual Port 5", sizeof(InputDeviceInfoPSXPort) / sizeof(InputDeviceInfoStruct), InputDeviceInfoPSXPort, "gamepad" },
 { "port6", "Virtual Port 6", sizeof(InputDeviceInfoPSXPort) / sizeof(InputDeviceInfoStruct), InputDeviceInfoPSXPort, "gamepad" },
 { "port7", "Virtual Port 7", sizeof(InputDeviceInfoPSXPort) / sizeof(InputDeviceInfoStruct), InputDeviceInfoPSXPort, "gamepad" },
 { "port8", "Virtual Port 8", sizeof(InputDeviceInfoPSXPort) / sizeof(InputDeviceInfoStruct), InputDeviceInfoPSXPort, "gamepad" },
};

InputInfoStruct FIO_InputInfo =
{
 sizeof(PortInfo) / sizeof(InputPortInfoStruct),
 PortInfo
};
