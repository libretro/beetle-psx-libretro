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

#include "../mednafen.h"
#include "../md5.h"
#include "../general.h"
#include "src/base.hpp"
#include "../mempatcher.h"
#include "Fir_Resampler.h"
#include <vector>

static void Cleanup(void);

static Fir_Resampler<24> resampler;
class MeowFace : public SNES::Interface
{
  virtual void video_refresh(uint16_t *data, unsigned pitch, unsigned *line, unsigned width, unsigned height);
  virtual void audio_sample(uint16_t l_sample, uint16_t r_sample);
  virtual void input_poll();
  virtual int16_t input_poll(bool port, unsigned device, unsigned index, unsigned id);
//  virtual int16_t input_poll(unsigned deviceid, unsigned id);
};

static bool InProperEmu;
static bool SoundOn;
static double SoundLastRate = 0;
static MeowFace meowface;

static int32 CycleCounter;
static MDFN_Surface *tsurf = NULL;
static MDFN_Rect *tlw = NULL;
static MDFN_Rect *tdr = NULL;

static int InputType[2];
static uint8 *InputPtr[8] = { NULL };
static uint16 PadLatch[8];
static bool MultitapEnabled[2];
static bool HasPolledThisFrame;

static int16 MouseXLatch[2];
static int16 MouseYLatch[2];
static uint8 MouseBLatch[2];

static uint8 *CustomColorMap = NULL;
//static uint32 ColorMap[32768];
static std::vector<uint32> ColorMap;

static bool LoadCPalette(const char *syspalname, uint8 **ptr, uint32 num_entries)
{
 std::string colormap_fn = MDFN_MakeFName(MDFNMKF_PALETTE, 0, syspalname).c_str();
 FILE *fp;

 MDFN_printf(_("Loading custom palette from \"%s\"...\n"),  colormap_fn.c_str());
 MDFN_indent(1);

 if(!(fp = fopen(colormap_fn.c_str(), "rb")))
 {
  ErrnoHolder ene(errno);

  MDFN_printf(_("Error opening file: %s\n"), ene.StrError());

  MDFN_indent(-1);

  return(ene.Errno() == ENOENT);        // Return fatal error if it's an error other than the file not being found.
 }

 if(!(*ptr = (uint8 *)MDFN_malloc(num_entries * 3, _("custom color map"))))
 {
  MDFN_indent(-1);

  fclose(fp);
  return(false);
 }

 if(fread(*ptr, 1, num_entries * 3, fp) != (num_entries * 3))
 {
  ErrnoHolder ene(errno);

  MDFN_printf(_("Error reading file: %s\n"), feof(fp) ? "EOF" : ene.StrError());
  MDFN_indent(-1);

  MDFN_free(*ptr);
  *ptr = NULL;
  fclose(fp);

  return(false);
 }

 fclose(fp);

 MDFN_indent(-1);

 return(true);
}


static void BuildColorMap(MDFN_PixelFormat &format)
{
 for(int x = 0; x < 32768; x++) 
 {
  int r, g, b;

  r = (x & (0x1F <<  0)) << 3;
  g = (x & (0x1F <<  5)) >> (5 - 3);
  b = (x & (0x1F << 10)) >> (5 * 2 - 3);

  //r = ((((x >> 0) & 0x1F) * 255 + 15) / 31);
  //g = ((((x >> 5) & 0x1F) * 255 + 15) / 31);
  //b = ((((x >> 10) & 0x1F) * 255 + 15) / 31);

  if(CustomColorMap)
  {
   r = CustomColorMap[x * 3 + 0];
   g = CustomColorMap[x * 3 + 1];
   b = CustomColorMap[x * 3 + 2];
  }

  ColorMap[x] = format.MakeColor(r, g, b);
 }
}

void MeowFace::video_refresh(uint16_t *data, unsigned pitch, unsigned *line, unsigned width, unsigned height)
{
 if(!tsurf || !tlw || !tdr)
  return;

 const uint16 *source_line = data;
 uint32 *dest_line = tsurf->pixels;

 assert(!(pitch & 1));

 //if(height != 224)
 // printf("%d\n", height);

 //if(tsurf->format.bpp == 32)
 //{
 //
 //}
 //else
 {
  for(int y = 0; y < height; y++, source_line += pitch >> 1, dest_line += tsurf->pitch32)
   for(int x = 0; x < width; tlw[y].x = 0, tlw[y].w = (width == 512) ? line[y] : 256, x++)
    dest_line[x] = ColorMap[source_line[x] & 0x7FFF];
 }

 tdr->w = width;
 tdr->h = height;
}

void MeowFace::audio_sample(uint16_t l_sample, uint16_t r_sample)
{
 CycleCounter++;

 if(!SoundOn)
  return;

 if(resampler.max_write() >= 2)
 {
  resampler.buffer()[0] = l_sample;
  resampler.buffer()[1] = r_sample;
  resampler.write(2);
 }
 else
 {
  MDFN_DispMessage("Buffer overflow?");
 }
}

#if 0
class Input {
public:
  enum Device {
    DeviceNone,
    DeviceJoypad,
    DeviceMultitap,
    DeviceMouse,
    DeviceSuperScope,
    DeviceJustifier,
    DeviceJustifiers,
  };

  enum JoypadID {
    JoypadB      =  0, JoypadY     =  1,
    JoypadSelect =  2, JoypadStart =  3,
    JoypadUp     =  4, JoypadDown  =  5,
    JoypadLeft   =  6, JoypadRight =  7,
    JoypadA      =  8, JoypadX     =  9,
    JoypadL      = 10, JoypadR     = 11,
  };
#endif

void MeowFace::input_poll()
{
 if(!InProperEmu)
  return;

 HasPolledThisFrame = true;

 for(int port = 0; port < 2; port++)
 {
  switch(InputType[port])
  {
   case SNES::Input::DeviceJoypad:
	PadLatch[port] = MDFN_de16lsb(InputPtr[port]);
	break;

   case SNES::Input::DeviceMultitap:
	for(int index = 0; index < 4; index++)
        {
         if(!index)
          PadLatch[port] = MDFN_de16lsb(InputPtr[port]);
         else
	 {
	  int pi = 2 + 3 * (port ^ 1) + (index - 1);
          PadLatch[pi] = MDFN_de16lsb(InputPtr[pi]);
	 }
        }
        break;

   case SNES::Input::DeviceMouse:
	MouseXLatch[port] = (int32)MDFN_de32lsb(InputPtr[port] + 0);
	MouseYLatch[port] = (int32)MDFN_de32lsb(InputPtr[port] + 4);
	MouseBLatch[port] = *(uint8 *)(InputPtr[port] + 8);
	break;
  }
 }
}

static INLINE int16 sats32tos16(int32 val)
{
 if(val > 32767)
  val = 32767;
 if(val < -32768)
  val = -32768;

 return(val);
}

int16_t MeowFace::input_poll(bool port, unsigned device, unsigned index, unsigned id)
{
 if(!HasPolledThisFrame)
  printf("input_poll(...) before input_poll() for frame, %d %d %d %d\n", port, device, index, id);

 switch(device)
 {
 	case SNES::Input::DeviceJoypad:
	{
	  return((PadLatch[port] >> id) & 1);
	}
	break;

	case SNES::Input::DeviceMultitap:
	{
	 if(!index)
          return((PadLatch[port] >> id) & 1);
         else
	  return((PadLatch[2 + 3 * (port ^ 1) + (index - 1)] >> id) & 1);
	}
	break;

	case SNES::Input::DeviceMouse:
	{
	 assert(port < 2);
	 switch(id)
	 {
	  case SNES::Input::MouseX:
		return(sats32tos16(MouseXLatch[port]));
		break;

	  case SNES::Input::MouseY:
		return(sats32tos16(MouseYLatch[port]));
		break;

	  case SNES::Input::MouseLeft:
		return((int)(bool)(MouseBLatch[port] & 1));
		break;

	  case SNES::Input::MouseRight:
		return((int)(bool)(MouseBLatch[port] & 2));
		break;
	 }
	}
	break;
 }

 return(0);
}

#if 0
void MeowFace::init()
{


}

void MeowFace::term()
{


}
#endif

#if 0

namespace memory {
  extern MappedRAM cartrom, cartram, cartrtc;
  extern MappedRAM bsxflash, bsxram, bsxpram;
  extern MappedRAM stArom, stAram;
  extern MappedRAM stBrom, stBram;
  extern MappedRAM gbrom, gbram;
};

#endif

// For loading: Return false on fatal error during loading, or true on success(or file not found)
static bool SaveMemorySub(bool load, const char *extension, SNES::MappedRAM *memoryA, SNES::MappedRAM *memoryB = NULL)
{
 const std::string path = MDFN_MakeFName(MDFNMKF_SAV, 0, extension);
 std::vector<PtrLengthPair> MemToSave;

 if(load)
 {
  FILE *gp;

  errno = 0;
  gp = fopen(path.c_str(), "rb");
  if(!gp)
  {
   ErrnoHolder ene(errno);
   if(ene.Errno() == ENOENT)
    return(true);

   MDFN_PrintError(_("Error opening save file \"%s\": %s"), path.c_str(), ene.StrError());
   return(false);
  }

  if(memoryA && memoryA->size() != 0 && memoryA->size() != -1U)
  {
   errno = 0;
   fread(memoryA->data(), memoryA->size(), 1, gp);
  }

  if(memoryB && memoryB->size() != 0 && memoryB->size() != -1U)
  {
   errno = 0;
   fread(memoryB->data(), memoryB->size(), 1, gp);
  }

  fclose(gp);

  return(true);
 }
 else
 {
  if(memoryA && memoryA->size() != 0 && memoryA->size() != -1U)
   MemToSave.push_back(PtrLengthPair(memoryA->data(), memoryA->size()));

  if(memoryB && memoryB->size() != 0 && memoryB->size() != -1U)
   MemToSave.push_back(PtrLengthPair(memoryB->data(), memoryB->size()));

  return(MDFN_DumpToFile(path.c_str(), 6, MemToSave));
 }
}

static bool SaveLoadMemory(bool load)
{
  if(SNES::cartridge.loaded() == false)
   return(FALSE);

  bool ret = true;

  switch(SNES::cartridge.mode())
  {
    case SNES::Cartridge::ModeNormal:
    case SNES::Cartridge::ModeBsxSlotted: 
    {
      ret &= SaveMemorySub(load, "srm", &SNES::memory::cartram);
      ret &= SaveMemorySub(load, "rtc", &SNES::memory::cartrtc);
    }
    break;

    case SNES::Cartridge::ModeBsx:
    {
      ret &= SaveMemorySub(load, "srm", &SNES::memory::bsxram );
      ret &= SaveMemorySub(load, "psr", &SNES::memory::bsxpram);
    }
    break;

    case SNES::Cartridge::ModeSufamiTurbo:
    {
     ret &= SaveMemorySub(load, "srm", &SNES::memory::stAram, &SNES::memory::stBram);
    }
    break;

    case SNES::Cartridge::ModeSuperGameBoy:
    {
     ret &= SaveMemorySub(load, "sav", &SNES::memory::gbram);
     ret &= SaveMemorySub(load, "rtc", &SNES::memory::gbrtc);
    }
    break;
  }

 return(ret);
}


static bool TestMagic(const char *name, MDFNFILE *fp)
{
 if(strcasecmp(fp->f_ext, "smc") && strcasecmp(fp->f_ext, "swc") && strcasecmp(fp->f_ext, "sfc") && strcasecmp(fp->f_ext, "fig") &&
        strcasecmp(fp->f_ext, "bs") && strcasecmp(fp->f_ext, "st"))
 {
  return(false);
 }

 return(true);
}

static void SetupMisc(bool PAL)
{
 MDFNGameInfo->fps = PAL ? 838977920 : 1008307711;
 MDFNGameInfo->MasterClock = MDFN_MASTERCLOCK_FIXED(32040.40);  //MDFN_MASTERCLOCK_FIXED(21477272);     //PAL ? PAL_CPU : NTSC_CPU);

 {
  MDFNGameInfo->nominal_width = MDFN_GetSettingB("snes.correct_aspect") ? 292 : 256;
  MDFNGameInfo->nominal_height = PAL ? 239 : 224;
  MDFNGameInfo->lcm_height = MDFNGameInfo->nominal_height * 2;
 }

 resampler.buffer_size(32040 / 10);
 //resampler.time_ratio((double)32040.40 / 48000, 0.9965);
 SoundLastRate = 0;
}

static bool LoadSNSF(MDFNFILE *fp)
{
 return(false);
}

static void Cleanup(void)
{
 SNES::memory::cartrom.map(NULL, 0); // So it delete[]s the pointer it took ownership of.

 if(CustomColorMap)
 {
  MDFN_free(CustomColorMap);
  CustomColorMap = NULL;
 }

 ColorMap.resize(0);
}

static int Load(const char *name, MDFNFILE *fp)
{
 bool PAL = FALSE;

 CycleCounter = 0;

 try
 {
  // Allocate 8MiB of space regardless of actual ROM image size, to prevent malformed or corrupted ROM images
  // from crashing the bsnes cart loading code.

  const uint32 header_adjust = (((fp->f_size & 0x7FFF) == 512) ? 512 : 0);
  uint8 *export_ptr;

  if((fp->f_size - header_adjust) > (8192 * 1024))
  {
   throw MDFN_Error(0, _("SNES ROM image is too large."));
  }

  md5_context md5;

  md5.starts();
  md5.update(fp->f_data, fp->f_size);
  md5.finish(MDFNGameInfo->MD5);

  SNES::system.init(&meowface);

  //const SNES::Cartridge::Type rom_type = SNES::cartridge.detect_image_type((uint8 *)fp->f_data, fp->size);

  export_ptr = new uint8[8192 * 1024];
  memset(export_ptr, 0x00, 8192 * 1024);
  memcpy(export_ptr, fp->f_data + header_adjust, fp->f_size - header_adjust);

  SNES::memory::cartrom.map(export_ptr, fp->f_size - header_adjust);

  SNES::cartridge.load(SNES::Cartridge::ModeNormal);

  SNES::system.power();

  PAL = (SNES::system.region() == SNES::System::PAL);

  SetupMisc(PAL);

  MultitapEnabled[0] = MDFN_GetSettingB("snes.input.port1.multitap");
  MultitapEnabled[1] = MDFN_GetSettingB("snes.input.port2.multitap");

  if(!SaveLoadMemory(true))
  {
   Cleanup();
   return(0);
  }

  //printf(" %d %d\n", FSettings.SndRate, resampler.max_write());

  MDFNMP_Init(1024, (1 << 24) / 1024);

  MDFNMP_AddRAM(131072, 0x7E << 16, SNES::memory::wram.data());

  ColorMap.resize(32768);

  if(!LoadCPalette(NULL, &CustomColorMap, 32768))
  {
   Cleanup();
   return(0);
  }
 }
 catch(std::exception &e)
 {
  MDFND_PrintError(e.what());
  Cleanup();
  return 0;
 }

 return(1);
}

static void CloseGame(void)
{
 {
  SaveLoadMemory(false);
 }
 Cleanup();
}

static void Emulate(EmulateSpecStruct *espec)
{
 tsurf = espec->surface;
 tlw = espec->LineWidths;
 tdr = &espec->DisplayRect;

 {
  if(espec->VideoFormatChanged)
   BuildColorMap(espec->surface->format);
 }

 if(SoundLastRate != espec->SoundRate)
 {
  double ratio = (double)32040.40 / (espec->SoundRate ? espec->SoundRate : 48000);
  resampler.time_ratio(ratio, 0.9965);
  printf("%f, %f\n", ratio, resampler.ratio());
  SoundLastRate = espec->SoundRate;
 }

  MDFNMP_ApplyPeriodicCheats();

 // Make sure to trash any leftover samples, generated from system.runtosave() in save state saving, if sound is now disabled.
 if(SoundOn && !espec->SoundBuf)
 {
  resampler.clear();
 }

 SoundOn = espec->SoundBuf ? true : false;

 HasPolledThisFrame = false;
 InProperEmu = TRUE;
 SNES::system.run_mednafen_custom();
 tsurf = NULL;
 tlw = NULL;
 tdr = NULL;
 InProperEmu = FALSE;

 espec->MasterCycles = CycleCounter;
 CycleCounter = 0;

 //printf("%d\n", espec->MasterCycles);

 if(espec->SoundBuf)
  espec->SoundBufSize = resampler.read(espec->SoundBuf, resampler.avail()) >> 1;

 MDFNGameInfo->mouse_sensitivity = MDFN_GetSettingF("snes.mouse_sensitivity");
}

static int StateAction(StateMem *sm, int load, int data_only)
{
 //if(!SNES::Cartridge::saveStatesSupported())
  //return(0);

 if(load)
 {
  uint32 length;
  uint8 *ptr;

  SFORMAT StateLengthCat[] =
  {
   SFVARN(length, "length"),
   SFEND
  };

  if(!MDFNSS_StateAction(sm, 1, data_only, StateLengthCat, "LEN"))
   return(0);

  ptr = (uint8 *)MDFN_calloc(1, length, _("SNES save state buffer"));
 
  SFORMAT StateRegs[] =
  {
   SFARRAYN(ptr, length, "OmniCat"),
   SFARRAY16(PadLatch, 8),
   SFARRAY16(MouseXLatch, 2),
   SFARRAY16(MouseYLatch, 2),
   SFARRAY(MouseBLatch, 2),
   SFEND
  };

  if(!MDFNSS_StateAction(sm, 1, data_only, StateRegs, "DATA"))
  {
   free(ptr);
   return(0);
  }

  serializer state(ptr, length);
  int result;
  
  result = SNES::system.unserialize(state);

  free(ptr);
  return(result);
 }
 else // save:
 {
  uint32 length;

  if(SNES::scheduler.sync != SNES::Scheduler::SyncAll)
   SNES::system.runtosave();

  serializer state = SNES::system.serialize();

  length = state.size();

  SFORMAT StateLengthCat[] =
  {
   SFVARN(length, "length"),
   SFEND
  };

  if(!MDFNSS_StateAction(sm, 0, data_only, StateLengthCat, "LEN"))
   return(0);

  uint8 *ptr = const_cast<uint8 *>(state.data());

  SFORMAT StateRegs[] =
  {
   SFARRAYN(ptr, length, "OmniCat"),
   SFARRAY16(PadLatch, 8),
   SFARRAY16(MouseXLatch, 2),
   SFARRAY16(MouseYLatch, 2),
   SFARRAY(MouseBLatch, 2),
   SFEND
  };

  if(!MDFNSS_StateAction(sm, 0, data_only, StateRegs, "DATA"))
   return(0);

  return(1);
 }

}

struct StrToBSIT_t
{
 const char *str;
 const int id;
};

static const StrToBSIT_t StrToBSIT[] =
{
 { "none",   	SNES::Input::DeviceNone },
 { "gamepad",   SNES::Input::DeviceJoypad },
 { "multitap",  SNES::Input::DeviceMultitap },
 { "mouse",   	SNES::Input::DeviceMouse },
 { "superscope",   SNES::Input::DeviceSuperScope },
 { "justifier",   SNES::Input::DeviceJustifier },
 { "justifiers",   SNES::Input::DeviceJustifiers },
 { NULL,	-1	},
};


static void SetInput(int port, const char *type, void *ptr)
{
 assert(port >= 0 && port < 8);

 if(port < 2)
 {
  const StrToBSIT_t *sb = StrToBSIT;
  int id = -1;

  if(MultitapEnabled[port] && !strcmp(type, "gamepad"))
   type = "multitap";

  while(sb->str && id == -1)
  {
   if(!strcmp(type, sb->str))
    id = sb->id;
   sb++;
  }
  assert(id != -1);

  InputType[port] = id;

  SNES::input.port_set_device(port, id);

#if 0
  switch(config().input.port1) { default:
    case ControllerPort1::None: mapper().port1 = 0; break;
    case ControllerPort1::Gamepad: mapper().port1 = &Controllers::gamepad1; break;
    case ControllerPort1::Asciipad: mapper().port1 = &Controllers::asciipad1; break;
    case ControllerPort1::Multitap: mapper().port1 = &Controllers::multitap1; break;
    case ControllerPort1::Mouse: mapper().port1 = &Controllers::mouse1; break;
  }

  switch(config().input.port2) { default:
    case ControllerPort2::None: mapper().port2 = 0; break;
    case ControllerPort2::Gamepad: mapper().port2 = &Controllers::gamepad2; break;
    case ControllerPort2::Asciipad: mapper().port2 = &Controllers::asciipad2; break;
    case ControllerPort2::Multitap: mapper().port2 = &Controllers::multitap2; break;
    case ControllerPort2::Mouse: mapper().port2 = &Controllers::mouse2; break;
    case ControllerPort2::SuperScope: mapper().port2 = &Controllers::superscope; break;
    case ControllerPort2::Justifier: mapper().port2 = &Controllers::justifier1; break;
    case ControllerPort2::Justifiers: mapper().port2 = &Controllers::justifiers; break;
  }
#endif

 }


 InputPtr[port] = (uint8 *)ptr;
}

static void SetLayerEnableMask(uint64 mask)
{

}


static void DoSimpleCommand(int cmd)
{
 switch(cmd)
 {
  case MDFN_MSC_RESET: SNES::system.reset(); break;
  case MDFN_MSC_POWER: SNES::system.power(); break;
 }
}

static const InputDeviceInputInfoStruct GamepadIDII[] =
{
 { "b", "B (center, lower)", 7, IDIT_BUTTON_CAN_RAPID, NULL },
 { "y", "Y (left)", 6, IDIT_BUTTON_CAN_RAPID, NULL },
 { "select", "SELECT", 4, IDIT_BUTTON, NULL },
 { "start", "START", 5, IDIT_BUTTON, NULL },
 { "up", "UP ↑", 0, IDIT_BUTTON, "down" },
 { "down", "DOWN ↓", 1, IDIT_BUTTON, "up" },
 { "left", "LEFT ←", 2, IDIT_BUTTON, "right" },
 { "right", "RIGHT →", 3, IDIT_BUTTON, "left" },
 { "a", "A (right)", 9, IDIT_BUTTON_CAN_RAPID, NULL },
 { "x", "X (center, upper)", 8, IDIT_BUTTON_CAN_RAPID, NULL },
 { "l", "Left Shoulder", 10, IDIT_BUTTON, NULL },
 { "r", "Right Shoulder", 11, IDIT_BUTTON, NULL },
};

static const InputDeviceInputInfoStruct MouseIDII[0x4] =
{
 { "x_axis", "X Axis", -1, IDIT_X_AXIS_REL },
 { "y_axis", "Y Axis", -1, IDIT_Y_AXIS_REL },
 { "left", "Left Button", 0, IDIT_BUTTON, NULL },
 { "right", "Right Button", 1, IDIT_BUTTON, NULL },
};


static InputDeviceInfoStruct InputDeviceInfoSNESPort[] =
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

 // Gamepad
 {
  "gamepad",
  "Gamepad",
  NULL,
  NULL,
  sizeof(GamepadIDII) / sizeof(InputDeviceInputInfoStruct),
  GamepadIDII,
 },

 // Mouse
 {
  "mouse",
  "Mouse",
  NULL,
  NULL,
  sizeof(MouseIDII) / sizeof(InputDeviceInputInfoStruct),
  MouseIDII,
 },

};


static InputDeviceInfoStruct InputDeviceInfoTapPort[] =
{
 // Gamepad
 {
  "gamepad",
  "Gamepad",
  NULL,
  NULL,
  sizeof(GamepadIDII) / sizeof(InputDeviceInputInfoStruct),
  GamepadIDII,
 },
};


static const InputPortInfoStruct PortInfo[] =
{
 { "port1", "Port 1/1A", sizeof(InputDeviceInfoSNESPort) / sizeof(InputDeviceInfoStruct), InputDeviceInfoSNESPort, "gamepad" },
 { "port2", "Port 2/2A", sizeof(InputDeviceInfoSNESPort) / sizeof(InputDeviceInfoStruct), InputDeviceInfoSNESPort, "gamepad" },
 { "port3", "Port 2B", sizeof(InputDeviceInfoTapPort) / sizeof(InputDeviceInfoStruct), InputDeviceInfoTapPort, "gamepad" },
 { "port4", "Port 2C", sizeof(InputDeviceInfoTapPort) / sizeof(InputDeviceInfoStruct), InputDeviceInfoTapPort, "gamepad" },
 { "port5", "Port 2D", sizeof(InputDeviceInfoTapPort) / sizeof(InputDeviceInfoStruct), InputDeviceInfoTapPort, "gamepad" },
 { "port6", "Port 1B", sizeof(InputDeviceInfoTapPort) / sizeof(InputDeviceInfoStruct), InputDeviceInfoTapPort, "gamepad" },
 { "port7", "Port 1C", sizeof(InputDeviceInfoTapPort) / sizeof(InputDeviceInfoStruct), InputDeviceInfoTapPort, "gamepad" },
 { "port8", "Port 1D", sizeof(InputDeviceInfoTapPort) / sizeof(InputDeviceInfoStruct), InputDeviceInfoTapPort, "gamepad" },
};

static InputInfoStruct SNESInputInfo =
{
 sizeof(PortInfo) / sizeof(InputPortInfoStruct),
 PortInfo
};

static const MDFNSetting SNESSettings[] =
{
 { "snes.input.port1.multitap", MDFNSF_EMU_STATE | MDFNSF_UNTRUSTED_SAFE, gettext_noop("Enable multitap on SNES port 1."), NULL, MDFNST_BOOL, "0", NULL, NULL },
 { "snes.input.port2.multitap", MDFNSF_EMU_STATE | MDFNSF_UNTRUSTED_SAFE, gettext_noop("Enable multitap on SNES port 2."), NULL, MDFNST_BOOL, "0", NULL, NULL },

 { "snes.mouse_sensitivity", MDFNSF_NOFLAGS, gettext_noop("Emulated mouse sensitivity."), NULL, MDFNST_FLOAT, "0.50", NULL, NULL, NULL },

 { "snes.correct_aspect", MDFNSF_CAT_VIDEO, gettext_noop("Correct the aspect ratio."), gettext_noop("Note that regardless of this setting's value, \"512\" and \"256\" width modes will be scaled to the same dimensions for display."), MDFNST_BOOL, "0" },

 { NULL }
};

static const FileExtensionSpecStruct KnownExtensions[] =
{
 { ".smc", "Super Magicom ROM Image" },
 { ".swc", "Super Wildcard ROM Image" },
 { ".sfc", "Cartridge ROM Image" },
 { ".fig", "Cartridge ROM Image" },

 { ".bs", "BS-X EEPROM Image" },
 { ".st", "Sufami Turbo Cartridge ROM Image" },

 { NULL, NULL }
};

MDFNGI EmulatedSNES =
{
 "snes",
 "Super Nintendo Entertainment System/Super Famicom",
 KnownExtensions,
 MODPRIO_INTERNAL_HIGH,
 NULL,						// Debugger
 &SNESInputInfo,
 Load,
 TestMagic,
 NULL,
 NULL,
 CloseGame,
 SetLayerEnableMask,
 NULL,	// Layer names, null-delimited
 NULL,
 NULL,
 NULL, //InstallReadPatch,
 NULL, //RemoveReadPatches,
 NULL, //MemRead,
 true,
 StateAction,
 Emulate,
 SetInput,
 DoSimpleCommand,
 SNESSettings,
 0,
 0,
 FALSE, // Multires

 512,   // lcm_width
 480,   // lcm_height           (replaced in game load)
 NULL,  // Dummy

 256,   // Nominal width	(replaced in game load)
 240,   // Nominal height	(replaced in game load)
 
 512,	// Framebuffer width
 512,	// Framebuffer height

 2,     // Number of output sound channels
};


