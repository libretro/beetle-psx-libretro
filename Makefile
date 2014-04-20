DEBUG = 0
FRONTEND_SUPPORTS_RGB565 = 1

MEDNAFEN_DIR := mednafen
MEDNAFEN_LIBRETRO_DIR := mednafen-libretro
NEED_TREMOR = 0
LIBRETRO_SOURCES :=

ifeq ($(platform),)
platform = unix
ifeq ($(shell uname -a),)
   platform = win
else ifneq ($(findstring Darwin,$(shell uname -a)),)
   platform = osx
   arch = intel
ifeq ($(shell uname -p),powerpc)
   arch = ppc
endif
else ifneq ($(findstring MINGW,$(shell uname -a)),)
   platform = win
endif
endif

# If you have a system with 1GB RAM or more - cache the whole 
# CD for CD-based systems in order to prevent file access delays/hiccups
CACHE_CD = 0

#if no core specified, just pick psx for now
ifeq ($(core),)
   core = psx
endif

ifeq ($(core), psx)
   core = psx
ifneq ($(platform), osx)
   PTHREAD_FLAGS = -pthread
endif
   NEED_CD = 1
   NEED_TREMOR = 1
   NEED_BPP = 32
	WANT_NEW_API = 1
   NEED_BLIP = 1
   NEED_DEINTERLACER = 1
   NEED_THREADING = 1
   NEED_STEREO_SOUND = 1
   CORE_DEFINE := -DWANT_PSX_EMU
   CORE_DIR := $(MEDNAFEN_DIR)/psx-09333
   CORE_SOURCES := $(CORE_DIR)/psx.cpp \
	$(CORE_DIR)/irq.cpp \
	$(CORE_DIR)/timer.cpp \
	$(CORE_DIR)/dma.cpp \
	$(CORE_DIR)/frontio.cpp \
	$(CORE_DIR)/sio.cpp \
	$(CORE_DIR)/cpu.cpp \
	$(CORE_DIR)/gte.cpp \
	$(CORE_DIR)/dis.cpp \
	$(CORE_DIR)/cdc.cpp \
	$(CORE_DIR)/spu.cpp \
	$(CORE_DIR)/gpu.cpp \
	$(CORE_DIR)/mdec.cpp \
	$(CORE_DIR)/input/gamepad.cpp \
	$(CORE_DIR)/input/dualanalog.cpp \
	$(CORE_DIR)/input/dualshock.cpp \
	$(CORE_DIR)/input/justifier.cpp \
	$(CORE_DIR)/input/guncon.cpp \
	$(CORE_DIR)/input/negcon.cpp \
	$(CORE_DIR)/input/memcard.cpp \
	$(CORE_DIR)/input/multitap.cpp \
	$(CORE_DIR)/input/mouse.cpp
   TARGET_NAME := mednafen_psx_libretro
else ifeq ($(core), pce-fast)
   core = pce_fast
ifneq ($(platform), osx)
   PTHREAD_FLAGS = -pthread
endif
   HAVE_HES = 0
   NEED_BPP = 16
   NEED_TREMOR = 1
   NEED_BLIP = 1
   NEED_CD = 1
   NEED_STEREO_SOUND = 1
   NEED_SCSI_CD = 1
   NEED_THREADING = 1
   NEED_CRC32 = 1
	WANT_NEW_API = 1
   CORE_DEFINE := -DWANT_PCE_FAST_EMU
   CORE_DIR := $(MEDNAFEN_DIR)/pce_fast-09333

CORE_SOURCES := \
   $(CORE_DIR)/huc.cpp \
	$(CORE_DIR)/huc6280.cpp \
	$(CORE_DIR)/input.cpp \
	$(CORE_DIR)/pce.cpp \
	$(CORE_DIR)/pcecd.cpp \
	$(CORE_DIR)/pcecd_drive.cpp \
	$(CORE_DIR)/psg.cpp \
	$(CORE_DIR)/vdc.cpp

ifeq ($(HAVE_HES),1)
CORE_SOURCES += $(CORE_DIR)/hes.cpp
endif
TARGET_NAME := mednafen_pce_fast_libretro

arch = intel
ifeq ($(shell uname -p),powerpc)
arch = ppc
endif

HW_CPU_SOURCES += $(MEDNAFEN_DIR)/hw_cpu/huc6280/cpu_huc6280.cpp
HW_MISC_SOURCES += $(MEDNAFEN_DIR)/hw_misc/arcade_card/arcade_card.cpp
HW_SOUND_SOURCES += $(MEDNAFEN_DIR)/hw_sound/pce_psg/pce_psg.cpp
HW_VIDEO_SOURCES += $(MEDNAFEN_DIR)/hw_video/huc6270/vdc_video.cpp
CDROM_SOURCES += $(MEDNAFEN_DIR)/cdrom/pcecd.cpp
OKIADPCM_SOURCES += $(MEDNAFEN_DIR)/okiadpcm.cpp
else ifeq ($(core), lynx)
   core = lynx
   NEED_BPP = 32
   NEED_BLIP = 1
   NEED_STEREO_SOUND = 1
   CORE_DEFINE := -DWANT_LYNX_EMU
   CORE_DIR := $(MEDNAFEN_DIR)/lynx
   NEED_CRC32 = 1

CORE_SOURCES := $(CORE_DIR)/cart.cpp \
	$(CORE_DIR)/c65c02.cpp \
	$(CORE_DIR)/memmap.cpp \
	$(CORE_DIR)/mikie.cpp \
	$(CORE_DIR)/ram.cpp \
	$(CORE_DIR)/rom.cpp \
	$(CORE_DIR)/susie.cpp \
	$(CORE_DIR)/system.cpp
TARGET_NAME := mednafen_lynx_libretro
else ifeq ($(core), wswan)
   core = wswan
   NEED_BPP = 32
   NEED_BLIP = 1
   NEED_STEREO_SOUND = 1
	WANT_NEW_API = 1
   CORE_DEFINE := -DWANT_WSWAN_EMU
   CORE_DIR := $(MEDNAFEN_DIR)/wswan-09333

CORE_SOURCES := $(CORE_DIR)/gfx.cpp \
	$(CORE_DIR)/main.cpp \
	$(CORE_DIR)/wswan-memory.cpp \
	$(CORE_DIR)/v30mz.cpp \
	$(CORE_DIR)/sound.cpp \
	$(CORE_DIR)/tcache.cpp \
	$(CORE_DIR)/interrupt.cpp \
	$(CORE_DIR)/eeprom.cpp \
	$(CORE_DIR)/rtc.cpp
TARGET_NAME := mednafen_wswan_libretro
else ifeq ($(core), ngp)
   core = ngp
   NEED_BPP = 32
   NEED_BLIP = 1
	WANT_NEW_API = 1
   NEED_STEREO_SOUND = 1
   CORE_DEFINE := -DWANT_NGP_EMU
   CORE_DIR := $(MEDNAFEN_DIR)/ngp-09333

CORE_SOURCES := $(CORE_DIR)/bios.cpp \
	$(CORE_DIR)/biosHLE.cpp \
	$(CORE_DIR)/dma.cpp \
	$(CORE_DIR)/flash.cpp \
	$(CORE_DIR)/gfx.cpp \
	$(CORE_DIR)/gfx_scanline_colour.cpp \
	$(CORE_DIR)/gfx_scanline_mono.cpp \
	$(CORE_DIR)/interrupt.cpp \
	$(CORE_DIR)/mem.cpp \
	$(CORE_DIR)/neopop.cpp \
	$(CORE_DIR)/rom.cpp \
	$(CORE_DIR)/rtc.cpp \
	$(CORE_DIR)/sound.cpp \
	$(CORE_DIR)/T6W28_Apu.cpp \
	$(CORE_DIR)/Z80_interface.cpp \
	$(CORE_DIR)/TLCS-900h/TLCS900h_disassemble.cpp \
	$(CORE_DIR)/TLCS-900h/TLCS900h_disassemble_extra.cpp \
	$(CORE_DIR)/TLCS-900h/TLCS900h_disassemble_reg.cpp \
	$(CORE_DIR)/TLCS-900h/TLCS900h_disassemble_dst.cpp \
	$(CORE_DIR)/TLCS-900h/TLCS900h_disassemble_src.cpp \
	$(CORE_DIR)/TLCS-900h/TLCS900h_interpret.cpp \
	$(CORE_DIR)/TLCS-900h/TLCS900h_interpret_dst.cpp \
	$(CORE_DIR)/TLCS-900h/TLCS900h_interpret_reg.cpp \
	$(CORE_DIR)/TLCS-900h/TLCS900h_interpret_single.cpp \
	$(CORE_DIR)/TLCS-900h/TLCS900h_interpret_src.cpp \
	$(CORE_DIR)/TLCS-900h/TLCS900h_registers.cpp

HW_CPU_SOURCES += $(MEDNAFEN_DIR)/hw_cpu/z80-fuse/z80.cpp \
						$(MEDNAFEN_DIR)/hw_cpu/z80-fuse/z80_ops.cpp
TARGET_NAME := mednafen_ngp_libretro
else ifeq ($(core), gba)
   core = gba
   NEED_BPP = 32
   NEED_BLIP = 1
   NEED_STEREO_SOUND = 1
   NEED_CRC32 = 1
   CORE_DEFINE := -DWANT_GBA_EMU
   CORE_DIR := $(MEDNAFEN_DIR)/gba

CORE_SOURCES := $(CORE_DIR)/arm.cpp \
	$(CORE_DIR)/bios.cpp \
	$(CORE_DIR)/eeprom.cpp \
	$(CORE_DIR)/flash.cpp \
	$(CORE_DIR)/GBA.cpp \
	$(CORE_DIR)/GBAinline.cpp \
	$(CORE_DIR)/Gfx.cpp \
	$(CORE_DIR)/Globals.cpp \
	$(CORE_DIR)/Mode0.cpp \
	$(CORE_DIR)/Mode1.cpp \
	$(CORE_DIR)/Mode2.cpp \
	$(CORE_DIR)/Mode3.cpp \
	$(CORE_DIR)/Mode4.cpp \
	$(CORE_DIR)/Mode5.cpp \
	$(CORE_DIR)/RTC.cpp \
	$(CORE_DIR)/Sound.cpp \
	$(CORE_DIR)/sram.cpp \
	$(CORE_DIR)/thumb.cpp

HW_SOUND_SOURCES += $(MEDNAFEN_DIR)/hw_sound/gb_apu/Gb_Apu.cpp \
						$(MEDNAFEN_DIR)/hw_sound/gb_apu/Gb_Apu_State.cpp \
						$(MEDNAFEN_DIR)/hw_sound/gb_apu/Gb_Oscs.cpp
EXTRA_CORE_INCDIR = -I$(MEDNAFEN_DIR)/hw_sound/ -I$(MEDNAFEN_DIR)/include/blip
TARGET_NAME := mednafen_$(core)_libretro
else ifeq ($(core), vb)
   core = vb
   NEED_BPP = 32
   NEED_BLIP = 1
	WANT_NEW_API = 1
   NEED_STEREO_SOUND = 1
   CORE_DEFINE := -DWANT_VB_EMU
   CORE_DIR := $(MEDNAFEN_DIR)/vb-09333

CORE_SOURCES := $(CORE_DIR)/input.cpp \
	$(CORE_DIR)/timer.cpp \
	$(CORE_DIR)/vb.cpp \
	$(CORE_DIR)/vip.cpp \
	$(CORE_DIR)/vsu.cpp

LIBRETRO_SOURCES_C := $(MEDNAFEN_DIR)/hw_cpu/v810/fpu-new/softfloat.c
HW_CPU_SOURCES += $(MEDNAFEN_DIR)/hw_cpu/v810/v810_cpu.cpp \
						$(MEDNAFEN_DIR)/hw_cpu/v810/v810_cpuD.cpp
EXTRA_CORE_INCDIR = -I$(MEDNAFEN_DIR)/hw_sound/ -I$(MEDNAFEN_DIR)/include/blip
TARGET_NAME := mednafen_$(core)_libretro
else ifeq ($(core), pcfx)
   core = pcfx
   NEED_BPP = 32
   NEED_BLIP = 1
   NEED_STEREO_SOUND = 1
   NEED_CD = 1
   NEED_SCSI_CD = 1
   NEED_THREADING = 1
   CORE_DEFINE := -DWANT_PCFX_EMU
   CORE_DIR := $(MEDNAFEN_DIR)/pcfx

CORE_SOURCES := $(CORE_DIR)/king.cpp \
	$(CORE_DIR)/soundbox.cpp \
	$(CORE_DIR)/pcfx.cpp \
	$(CORE_DIR)/interrupt.cpp \
	$(CORE_DIR)/input.cpp \
	$(CORE_DIR)/timer.cpp \
	$(CORE_DIR)/rainbow.cpp \
	$(CORE_DIR)/jrevdct.cpp \
	$(CORE_DIR)/huc6273.cpp \
	$(CORE_DIR)/fxscsi.cpp \
	$(CORE_DIR)/input/gamepad.cpp \
	$(CORE_DIR)/input/mouse.cpp

LIBRETRO_SOURCES_C := $(MEDNAFEN_DIR)/hw_cpu/v810/fpu-new/softfloat.c
HW_CPU_SOURCES += $(MEDNAFEN_DIR)/hw_cpu/v810/v810_cpu.cpp \
						$(MEDNAFEN_DIR)/hw_cpu/v810/v810_cpuD.cpp
HW_SOUND_SOURCES += $(MEDNAFEN_DIR)/hw_sound/pce_psg/pce_psg.cpp
HW_VIDEO_SOURCES += $(MEDNAFEN_DIR)/hw_video/huc6270/vdc.cpp
EXTRA_CORE_INCDIR = -I$(MEDNAFEN_DIR)/hw_sound/ -I$(MEDNAFEN_DIR)/include/blip -I$(MEDNAFEN_DIR)/hw_video/huc6270
TARGET_NAME := mednafen_$(core)_libretro
else ifeq ($(core), snes)
   core = snes
   NEED_BPP = 32
   NEED_BLIP = 1
   NEED_STEREO_SOUND = 1
   CORE_DEFINE := -DWANT_SNES_EMU
   CORE_DIR := $(MEDNAFEN_DIR)/snes

CORE_SOURCES := $(CORE_DIR)/interface.cpp \
	$(CORE_DIR)/src/cartridge/cartridge.cpp \
	$(CORE_DIR)/src/cartridge/header.cpp \
	$(CORE_DIR)/src/cartridge/gameboyheader.cpp \
	$(CORE_DIR)/src/cartridge/serialization.cpp \
	$(CORE_DIR)/src/cheat/cheat.cpp \
	$(CORE_DIR)/src/chip/21fx/21fx.cpp \
	$(CORE_DIR)/src/chip/bsx/bsx.cpp \
	$(CORE_DIR)/src/chip/bsx/bsx_base.cpp \
	$(CORE_DIR)/src/chip/bsx/bsx_cart.cpp \
	$(CORE_DIR)/src/chip/bsx/bsx_flash.cpp \
	$(CORE_DIR)/src/chip/cx4/cx4.cpp \
	$(CORE_DIR)/src/chip/cx4/data.cpp \
	$(CORE_DIR)/src/chip/cx4/functions.cpp \
	$(CORE_DIR)/src/chip/cx4/oam.cpp \
	$(CORE_DIR)/src/chip/cx4/opcodes.cpp \
	$(CORE_DIR)/src/chip/cx4/serialization.cpp \
	$(CORE_DIR)/src/chip/dsp1/dsp1.cpp \
	$(CORE_DIR)/src/chip/dsp2/dsp2.cpp \
	$(CORE_DIR)/src/chip/dsp3/dsp3.cpp \
	$(CORE_DIR)/src/chip/dsp4/dsp4.cpp \
	$(CORE_DIR)/src/chip/obc1/obc1.cpp \
	$(CORE_DIR)/src/chip/sa1/sa1.cpp \
	$(CORE_DIR)/src/chip/sdd1/sdd1.cpp \
	$(CORE_DIR)/src/chip/spc7110/spc7110.cpp \
	$(CORE_DIR)/src/chip/srtc/srtc.cpp \
	$(CORE_DIR)/src/chip/st010/st010.cpp \
	$(CORE_DIR)/src/chip/st011/st011.cpp \
	$(CORE_DIR)/src/chip/st018/st018.cpp \
	$(CORE_DIR)/src/chip/superfx/superfx.cpp \
	$(CORE_DIR)/src/chip/supergameboy/supergameboy.cpp \
	$(CORE_DIR)/src/cpu/cpu.cpp \
	$(CORE_DIR)/src/cpu/core/core.cpp \
	$(CORE_DIR)/src/cpu/scpu/scpu.cpp \
	$(CORE_DIR)/src/dsp/sdsp/sdsp.cpp \
	$(CORE_DIR)/src/memory/memory.cpp \
	$(CORE_DIR)/src/memory/smemory/smemory.cpp \
	$(CORE_DIR)/src/ppu/ppu.cpp \
	$(CORE_DIR)/src/ppu/bppu/bppu.cpp \
	$(CORE_DIR)/src/smp/smp.cpp \
	$(CORE_DIR)/src/smp/core/core.cpp \
	$(CORE_DIR)/src/smp/ssmp/ssmp.cpp \
	$(CORE_DIR)/src/system/system.cpp

LIBRETRO_SOURCES_C := $(CORE_DIR)/src/lib/libco/libco.c

HW_SOUND_SOURCES += $(MEDNAFEN_DIR)/sound/Fir_Resampler.cpp
EXTRA_CORE_INCDIR = -I$(MEDNAFEN_DIR)/hw_sound/ -I$(MEDNAFEN_DIR)/include/blip -I$(MEDNAFEN_DIR)/snes/src/lib
TARGET_NAME := mednafen_snes_libretro
LDFLAGS += -ldl
endif

ifeq ($(NEED_BLIP), 1)
RESAMPLER_SOURCES += $(MEDNAFEN_DIR)/sound/Blip_Buffer.cpp
endif

ifeq ($(NEED_STEREO_SOUND), 1)
SOUND_DEFINE := -DWANT_STEREO_SOUND
endif

CORE_INCDIR := -I$(CORE_DIR)

ifeq ($(platform), unix)
   TARGET := $(TARGET_NAME).so
   fpic := -fPIC
   SHARED := -shared -Wl,--no-undefined -Wl,--version-script=link.T
   ENDIANNESS_DEFINES := -DLSB_FIRST
   ifneq ($(shell uname -p | grep -E '((i.|x)86|amd64)'),)
      IS_X86 = 1
   endif
   LDFLAGS += $(PTHREAD_FLAGS)
   FLAGS += $(PTHREAD_FLAGS) -DHAVE_MKDIR
else ifeq ($(platform), osx)
   TARGET := $(TARGET_NAME).dylib
   fpic := -fPIC
   SHARED := -dynamiclib
   LDFLAGS += $(PTHREAD_FLAGS)
   FLAGS += $(PTHREAD_FLAGS) -DHAVE_MKDIR
ifeq ($(arch),ppc)
   ENDIANNESS_DEFINES := -DMSB_FIRST -DBYTE_ORDER=BIG_ENDIAN
   OLD_GCC := 1
else
   fpic += -fPIC -mmacosx-version-min=10.6
   ENDIANNESS_DEFINES := -DLSB_FIRST
endif
else ifeq ($(platform), ios)
   TARGET := $(TARGET_NAME)_ios.dylib
   fpic := -fPIC
   SHARED := -dynamiclib
   ENDIANNESS_DEFINES := -DLSB_FIRST
   LDFLAGS += $(PTHREAD_FLAGS) -miphone-version-min=5.0
   FLAGS += $(PTHREAD_FLAGS) -miphone-version-min=5.0

   CC = clang -arch armv7 -isysroot $(IOSSDK) -miphone-version-min=5.0
   CXX = clang++ -arch armv7 -isysroot $(IOSSDK) -miphone-version-min=5.0
else ifeq ($(platform), qnx)
   TARGET := $(TARGET_NAME)_qnx.so
   fpic := -fPIC
   SHARED := -lcpp -lm -shared -Wl,--no-undefined -Wl,--version-script=link.T
   ENDIANNESS_DEFINES := -DLSB_FIRST
   #LDFLAGS += $(PTHREAD_FLAGS)
   #FLAGS += $(PTHREAD_FLAGS) -DHAVE_MKDIR
   FLAGS += -DHAVE_MKDIR
   CC = qcc -Vgcc_ntoarmv7le
   CXX = QCC -Vgcc_ntoarmv7le_cpp
   AR = QCC -Vgcc_ntoarmv7le
   FLAGS += -D__BLACKBERRY_QNX__ -marm -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=softfp
else ifeq ($(platform), ps3)
   TARGET := $(TARGET_NAME)_ps3.a
   CC = $(CELL_SDK)/host-win32/ppu/bin/ppu-lv2-gcc.exe
   CXX = $(CELL_SDK)/host-win32/ppu/bin/ppu-lv2-g++.exe
   AR = $(CELL_SDK)/host-win32/ppu/bin/ppu-lv2-ar.exe
   ENDIANNESS_DEFINES := -DMSB_FIRST -DBYTE_ORDER=BIG_ENDIAN
   OLD_GCC := 1
   FLAGS += -DHAVE_MKDIR
   STATIC_LINKING = 1
else ifeq ($(platform), sncps3)
   TARGET := $(TARGET_NAME)_ps3.a
   CC = $(CELL_SDK)/host-win32/sn/bin/ps3ppusnc.exe
   CXX = $(CELL_SDK)/host-win32/sn/bin/ps3ppusnc.exe
   AR = $(CELL_SDK)/host-win32/sn/bin/ps3snarl.exe
   ENDIANNESS_DEFINES := -DMSB_FIRST -DBYTE_ORDER=BIG_ENDIAN
   CXXFLAGS += -Xc+=exceptions
   OLD_GCC := 1
   NO_GCC := 1
   FLAGS += -DHAVE_MKDIR
   STATIC_LINKING = 1
else ifeq ($(platform), psl1ght)
   TARGET := $(TARGET_NAME)_psl1ght.a
   CC = $(PS3DEV)/ppu/bin/ppu-gcc$(EXE_EXT)
   CXX = $(PS3DEV)/ppu/bin/ppu-g++$(EXE_EXT)
   AR = $(PS3DEV)/ppu/bin/ppu-ar$(EXE_EXT)
   ENDIANNESS_DEFINES := -DMSB_FIRST -DBYTE_ORDER=BIG_ENDIAN
   FLAGS += -DHAVE_MKDIR
   STATIC_LINKING = 1
else ifeq ($(platform), psp1)
   TARGET := $(TARGET_NAME)_psp1.a
   CC = psp-gcc$(EXE_EXT)
   CXX = psp-g++$(EXE_EXT)
   AR = psp-ar$(EXE_EXT)
   ENDIANNESS_DEFINES := -DLSB_FIRST
   FLAGS += -DPSP -G0
   FLAGS += -DHAVE_MKDIR
   STATIC_LINKING = 1
else ifeq ($(platform), xenon)
   TARGET := $(TARGET_NAME)_xenon360.a
   CC = xenon-gcc$(EXE_EXT)
   CXX = xenon-g++$(EXE_EXT)
   AR = xenon-ar$(EXE_EXT)
   ENDIANNESS_DEFINES += -D__LIBXENON__ -m32 -D__ppc__ -DMSB_FIRST -DBYTE_ORDER=BIG_ENDIAN
   LIBS := $(PTHREAD_FLAGS)
   FLAGS += -DHAVE_MKDIR
   STATIC_LINKING = 1
else ifeq ($(platform), ngc)
   TARGET := $(TARGET_NAME)_ngc.a
   CC = $(DEVKITPPC)/bin/powerpc-eabi-gcc$(EXE_EXT)
   CXX = $(DEVKITPPC)/bin/powerpc-eabi-g++$(EXE_EXT)
   AR = $(DEVKITPPC)/bin/powerpc-eabi-ar$(EXE_EXT)
   ENDIANNESS_DEFINES += -DGEKKO -DHW_DOL -mrvl -mcpu=750 -meabi -mhard-float -DMSB_FIRST -DBYTE_ORDER=BIG_ENDIAN

   EXTRA_INCLUDES := -I$(DEVKITPRO)/libogc/include
   FLAGS += -DHAVE_MKDIR
   STATIC_LINKING = 1
else ifeq ($(platform), wii)
   TARGET := $(TARGET_NAME)_wii.a
   CC = $(DEVKITPPC)/bin/powerpc-eabi-gcc$(EXE_EXT)
   CXX = $(DEVKITPPC)/bin/powerpc-eabi-g++$(EXE_EXT)
   AR = $(DEVKITPPC)/bin/powerpc-eabi-ar$(EXE_EXT)
   ENDIANNESS_DEFINES += -DGEKKO -DHW_RVL -mrvl -mcpu=750 -meabi -mhard-float -DMSB_FIRST -DBYTE_ORDER=BIG_ENDIAN

   EXTRA_INCLUDES := -I$(DEVKITPRO)/libogc/include
   FLAGS += -DHAVE_MKDIR
   STATIC_LINKING = 1
else ifneq (,$(findstring armv,$(platform)))
   TARGET := $(TARGET_NAME).so
   fpic := -fPIC
   SHARED := -shared -Wl,--no-undefined -Wl,--version-script=link.T
   ENDIANNESS_DEFINES := -DLSB_FIRST
   CC = gcc
   LDFLAGS += $(PTHREAD_FLAGS)
   FLAGS += $(PTHREAD_FLAGS) -DHAVE_MKDIR
   IS_X86 = 0
ifneq (,$(findstring cortexa8,$(platform)))
   FLAGS += -marm -mcpu=cortex-a8
   ASFLAGS += -mcpu=cortex-a8
else ifneq (,$(findstring cortexa9,$(platform)))
   FLAGS += -marm -mcpu=cortex-a9
   ASFLAGS += -mcpu=cortex-a9
endif
   FLAGS += -marm
ifneq (,$(findstring neon,$(platform)))
   FLAGS += -mfpu=neon
   ASFLAGS += -mfpu=neon
   HAVE_NEON = 1
endif
ifneq (,$(findstring softfloat,$(platform)))
   FLAGS += -mfloat-abi=softfp
else ifneq (,$(findstring hardfloat,$(platform)))
   FLAGS += -mfloat-abi=hard
endif
   FLAGS += -DARM
else
   TARGET := $(TARGET_NAME).dll
   CC = gcc
   CXX = g++
   IS_X86 = 1
   SHARED := -shared -Wl,--no-undefined -Wl,--version-script=link.T
   LDFLAGS += -static-libgcc -static-libstdc++ -lwinmm
   ENDIANNESS_DEFINES := -DLSB_FIRST
   FLAGS += -DHAVE__MKDIR
endif

ifeq ($(NEED_THREADING), 1)
   FLAGS += -DWANT_THREADING
endif

ifeq ($(NEED_CRC32), 1)
   FLAGS += -DWANT_CRC32
	LIBRETRO_SOURCES += scrc32.cpp
endif

ifeq ($(NEED_DEINTERLACER), 1)
   FLAGS += -DNEED_DEINTERLACER
endif

ifeq ($(NEED_SCSI_CD), 1)
   CDROM_SOURCES += $(MEDNAFEN_DIR)/cdrom/scsicd.cpp
endif

ifeq ($(NEED_CD), 1)
CDROM_SOURCES += $(MEDNAFEN_DIR)/cdrom/CDAccess.cpp \
	$(MEDNAFEN_DIR)/cdrom/CDAccess_Image.cpp \
	$(MEDNAFEN_DIR)/cdrom/CDAccess_CCD.cpp \
	$(MEDNAFEN_DIR)/cdrom/CDUtility.cpp \
	$(MEDNAFEN_DIR)/cdrom/lec.cpp \
	$(MEDNAFEN_DIR)/cdrom/SimpleFIFO.cpp \
	$(MEDNAFEN_DIR)/cdrom/audioreader.cpp \
	$(MEDNAFEN_DIR)/cdrom/galois.cpp \
	$(MEDNAFEN_DIR)/cdrom/recover-raw.cpp \
	$(MEDNAFEN_DIR)/cdrom/l-ec.cpp \
	$(MEDNAFEN_DIR)/cdrom/crc32.cpp \
	$(MEDNAFEN_DIR)/cdrom/cdromif.cpp
   FLAGS += -DNEED_CD
endif

ifeq ($(NEED_TREMOR), 1)
   TREMOR_SRC := $(wildcard $(MEDNAFEN_DIR)/tremor/*.c)
   FLAGS += -DNEED_TREMOR
endif


MEDNAFEN_SOURCES := $(MEDNAFEN_DIR)/mednafen.cpp \
	$(MEDNAFEN_DIR)/error.cpp \
	$(MEDNAFEN_DIR)/math_ops.cpp \
	$(MEDNAFEN_DIR)/settings.cpp \
	$(MEDNAFEN_DIR)/general.cpp \
	$(MEDNAFEN_DIR)/FileWrapper.cpp \
	$(MEDNAFEN_DIR)/FileStream.cpp \
	$(MEDNAFEN_DIR)/MemoryStream.cpp \
	$(MEDNAFEN_DIR)/Stream.cpp \
	$(MEDNAFEN_DIR)/state.cpp \
	$(MEDNAFEN_DIR)/endian.cpp \
	$(CDROM_SOURCES) \
	$(MEDNAFEN_DIR)/mempatcher.cpp \
	$(MEDNAFEN_DIR)/video/Deinterlacer.cpp \
	$(MEDNAFEN_DIR)/video/surface.cpp \
	$(RESAMPLER_SOURCES) \
	$(MEDNAFEN_DIR)/sound/Stereo_Buffer.cpp \
	$(MEDNAFEN_DIR)/file.cpp \
	$(MEDNAFEN_DIR)/player.cpp \
	$(OKIADPCM_SOURCES) \
	$(MEDNAFEN_DIR)/md5.cpp


LIBRETRO_SOURCES += libretro.cpp stubs.cpp $(THREAD_STUBS)

TRIO_SOURCES += $(MEDNAFEN_DIR)/trio/trio.c \
	$(MEDNAFEN_DIR)/trio/triostr.c

SOURCES_C := 	$(TREMOR_SRC) $(LIBRETRO_SOURCES_C) $(TRIO_SOURCES)

SOURCES := $(LIBRETRO_SOURCES) $(CORE_SOURCES) $(MEDNAFEN_SOURCES) $(HW_CPU_SOURCES) $(HW_MISC_SOURCES) $(HW_SOUND_SOURCES) $(HW_VIDEO_SOURCES)

WARNINGS := -Wall \
	-Wno-sign-compare \
	-Wno-unused-variable \
	-Wno-unused-function \
	-Wno-uninitialized \
	$(NEW_GCC_WARNING_FLAGS) \
	-Wno-strict-aliasing

EXTRA_GCC_FLAGS := -funroll-loops

ifeq ($(NO_GCC),1)
	EXTRA_GCC_FLAGS :=
	WARNINGS :=
else
	EXTRA_GCC_FLAGS := -g
endif


OBJECTS := $(SOURCES:.cpp=.o) $(SOURCES_C:.c=.o)

all: $(TARGET)

ifeq ($(DEBUG),0)
   FLAGS += -O2 $(EXTRA_GCC_FLAGS)
else
   FLAGS += -O0
endif

LDFLAGS += $(fpic) $(SHARED)
FLAGS += $(fpic) $(NEW_GCC_FLAGS)
FLAGS += -I. -Imednafen -Imednafen/include -Imednafen/intl -Imednafen/hw_misc -Imednafen/hw_sound -Imednafen/hw_cpu $(CORE_INCDIR) $(EXTRA_CORE_INCDIR)

FLAGS += $(ENDIANNESS_DEFINES) -DSIZEOF_DOUBLE=8 $(WARNINGS) -DMEDNAFEN_VERSION=\"0.9.31\" -DPACKAGE=\"mednafen\" -DMEDNAFEN_VERSION_NUMERIC=931 -DPSS_STYLE=1 -DMPC_FIXED_POINT $(CORE_DEFINE) -DSTDC_HEADERS -D__STDC_LIMIT_MACROS -D__LIBRETRO__ -D_LOW_ACCURACY_ $(EXTRA_INCLUDES) $(SOUND_DEFINE) -Dgettext_noop\(a\)=a

ifeq ($(IS_X86), 1)
FLAGS += -DARCH_X86
endif

ifeq ($(CACHE_CD), 1)
FLAGS += -D__LIBRETRO_CACHE_CD__
endif

ifeq ($(NEED_BPP), 8)
FLAGS += -DWANT_8BPP
endif

ifeq ($(NEED_BPP), 16)
FLAGS += -DWANT_16BPP
endif

ifeq ($(FRONTEND_SUPPORTS_RGB565), 1)
FLAGS += -DFRONTEND_SUPPORTS_RGB565
endif

ifeq ($(NEED_BPP), 32)
FLAGS += -DWANT_32BPP
endif

ifeq ($(WANT_NEW_API), 1)
FLAGS += -DWANT_NEW_API
endif

CXXFLAGS += $(FLAGS)
CFLAGS += $(FLAGS)

$(TARGET): $(OBJECTS)
ifeq ($(STATIC_LINKING), 1)
	$(AR) rcs $@ $(OBJECTS)
else
	$(CXX) -o $@ $^ $(LDFLAGS)
endif

%.o: %.cpp
	$(CXX) -c -o $@ $< $(CXXFLAGS)

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

clean:
	rm -f $(TARGET) $(OBJECTS)

.PHONY: clean
