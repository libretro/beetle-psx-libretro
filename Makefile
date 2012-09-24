
DEBUG = 0

ifeq ($(platform),)
platform = unix
ifeq ($(shell uname -a),)
   platform = win
else ifneq ($(findstring Darwin,$(shell uname -a)),)
   platform = osx
else ifneq ($(findstring MINGW,$(shell uname -a)),)
   platform = win
endif
endif

ifeq ($(platform), unix)
   TARGET := libretro.so
   fpic := -fPIC
   SHARED := -shared -Wl,--no-undefined -Wl,--version-script=link.T
   ENDIANNESS_DEFINES := -DLSB_FIRST
   LDFLAGS += -pthread
   FLAGS += -pthread -DHAVE_MKDIR
else ifeq ($(platform), osx)
   TARGET := libretro.dylib
   fpic := -fPIC
   SHARED := -dynamiclib
   ENDIANNESS_DEFINES := -DLSB_FIRST
   LDFLAGS += -pthread
   FLAGS += -pthread -DHAVE_MKDIR
else
   TARGET := retro.dll
   CC = i686-pc-mingw32-gcc
   CXX = i686-pc-mingw32-g++
   SHARED := -shared -Wl,--no-undefined -Wl,--version-script=link.T
   LDFLAGS += -static-libgcc -static-libstdc++ -lwinmm
   ENDIANNESS_DEFINES := -DLSB_FIRST
   FLAGS += -DHAVE__MKDIR
endif


MEDNAFEN_DIR := mednafen
MEDNAFEN_LIBRETRO_DIR := mednafen-libretro
PSX_DIR := $(MEDNAFEN_DIR)/psx

PSX_SOURCES := $(PSX_DIR)/psx.cpp \
	$(PSX_DIR)/irq.cpp \
	$(PSX_DIR)/timer.cpp \
	$(PSX_DIR)/dma.cpp \
	$(PSX_DIR)/frontio.cpp \
	$(PSX_DIR)/sio.cpp \
	$(PSX_DIR)/cpu.cpp \
	$(PSX_DIR)/gte.cpp \
	$(PSX_DIR)/dis.cpp \
	$(PSX_DIR)/cdc.cpp \
	$(MEDNAFEN_LIBRETRO_DIR)/psx/spu.cpp \
	$(PSX_DIR)/gpu.cpp \
	$(PSX_DIR)/mdec.cpp \
	$(PSX_DIR)/input/gamepad.cpp \
	$(PSX_DIR)/input/dualanalog.cpp \
	$(PSX_DIR)/input/memcard.cpp \
	$(PSX_DIR)/input/multitap.cpp \
	$(PSX_DIR)/input/mouse.cpp

MEDNAFEN_SOURCES := $(MEDNAFEN_DIR)/cdrom/cdromif.cpp \
	$(MEDNAFEN_LIBRETRO_DIR)/mednafen-libretro.cpp \
	$(MEDNAFEN_DIR)/PSFLoader.cpp \
	$(MEDNAFEN_DIR)/error.cpp \
	$(MEDNAFEN_DIR)/math_ops.cpp \
	$(MEDNAFEN_DIR)/settings.cpp \
	$(MEDNAFEN_DIR)/general.cpp \
	$(MEDNAFEN_DIR)/player.cpp \
	$(MEDNAFEN_LIBRETRO_DIR)/cdplay.cpp \
	$(MEDNAFEN_DIR)/FileWrapper.cpp \
	$(MEDNAFEN_DIR)/state.cpp \
	$(MEDNAFEN_DIR)/tests.cpp \
	$(MEDNAFEN_DIR)/endian.cpp \
	$(MEDNAFEN_DIR)/cdrom/CDAccess.cpp \
	$(MEDNAFEN_DIR)/cdrom/CDAccess_Image.cpp \
	$(MEDNAFEN_DIR)/cdrom/CDUtility.cpp \
	$(MEDNAFEN_DIR)/cdrom/lec.cpp \
	$(MEDNAFEN_DIR)/cdrom/SimpleFIFO.cpp \
	$(MEDNAFEN_DIR)/cdrom/audioreader.cpp \
	$(MEDNAFEN_DIR)/cdrom/galois.cpp \
	$(MEDNAFEN_DIR)/cdrom/pcecd.cpp \
	$(MEDNAFEN_DIR)/cdrom/scsicd.cpp \
	$(MEDNAFEN_DIR)/cdrom/recover-raw.cpp \
	$(MEDNAFEN_DIR)/cdrom/l-ec.cpp \
	$(MEDNAFEN_DIR)/cdrom/crc32.cpp \
	$(MEDNAFEN_DIR)/memory.cpp \
	$(MEDNAFEN_DIR)/mempatcher.cpp \
	$(MEDNAFEN_DIR)/video/video.cpp \
	$(MEDNAFEN_DIR)/video/text.cpp \
	$(MEDNAFEN_DIR)/video/font-data.cpp \
	$(MEDNAFEN_DIR)/video/tblur.cpp \
	$(MEDNAFEN_DIR)/video/Deinterlacer.cpp \
	$(MEDNAFEN_DIR)/video/surface.cpp \
	$(MEDNAFEN_DIR)/video/resize.cpp \
	$(MEDNAFEN_DIR)/string/escape.cpp \
	$(MEDNAFEN_DIR)/string/ConvertUTF.cpp \
	$(MEDNAFEN_DIR)/sound/Blip_Buffer.cpp \
	$(MEDNAFEN_DIR)/sound/Fir_Resampler.cpp \
	$(MEDNAFEN_DIR)/sound/Stereo_Buffer.cpp \
	$(MEDNAFEN_DIR)/sound/WAVRecord.cpp \
	$(MEDNAFEN_DIR)/sound/sound.cpp \
	$(MEDNAFEN_DIR)/file.cpp \
	$(MEDNAFEN_DIR)/okiadpcm.cpp \
	$(MEDNAFEN_DIR)/md5.cpp

MPC_SRC := $(wildcard $(MEDNAFEN_DIR)/mpcdec/*.c)
TREMOR_SRC := $(wildcard $(MEDNAFEN_DIR)/tremor/*.c)

LIBRETRO_SOURCES := libretro.cpp stubs.cpp thread.cpp

SOURCES_C := $(MEDNAFEN_DIR)/trio/trio.c \
	$(MPC_SRC) \
	$(TREMOR_SRC) \
	$(LIBRETRO_SOURCES_C) \
	$(MEDNAFEN_DIR)/trio/trionan.c \
	$(MEDNAFEN_DIR)/trio/triostr.c \
	$(MEDNAFEN_DIR)/string/world_strtod.c \
	$(MEDNAFEN_DIR)/compress/blz.c \
	$(MEDNAFEN_DIR)/compress/unzip.c \
	$(MEDNAFEN_DIR)/compress/minilzo.c \
	$(MEDNAFEN_DIR)/compress/quicklz.c \
	$(MEDNAFEN_DIR)/compress/ioapi.c

SOURCES := $(LIBRETRO_SOURCES) $(PSX_SOURCES) $(MEDNAFEN_SOURCES)
OBJECTS := $(SOURCES:.cpp=.o) $(SOURCES_C:.c=.o)

all: $(TARGET)

ifeq ($(DEBUG),0)
   FLAGS += -O3 -ffast-math -funroll-loops 
else
   FLAGS += -O0 -g
endif

LDFLAGS += $(fpic) -lz $(SHARED)
FLAGS += -msse -msse2 -Wall $(fpic) -fno-strict-overflow
FLAGS += -I. -Imednafen -Imednafen/include -Imednafen/intl -Imednafen/psx

WARNINGS := -Wall \
	-Wno-narrowing \
	-Wno-unused-but-set-variable \
	-Wno-sign-compare \
	-Wno-unused-variable \
	-Wno-unused-function \
	-Wno-uninitialized \
	-Wno-unused-result \
	-Wno-strict-aliasing \
	-Wno-overflow

FLAGS += $(ENDIANNESS_DEFINES) -DSIZEOF_DOUBLE=8 $(WARNINGS) \
			-DMEDNAFEN_VERSION=\"0.9.24\" -DPACKAGE=\"mednafen\" -DMEDNAFEN_VERSION_NUMERIC=924 -DPSS_STYLE=1 -DMPC_FIXED_POINT -DARCH_X86 \
			-DWANT_PSX_EMU -DSTDC_HEADERS

CXXFLAGS += $(FLAGS)
CFLAGS += $(FLAGS) -std=gnu99

$(TARGET): $(OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) -c -o $@ $< $(CXXFLAGS)

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

clean:
	rm -f $(TARGET) $(OBJECTS)

.PHONY: clean
