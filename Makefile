DEBUG = 0
FRONTEND_SUPPORTS_RGB565 = 1
HAVE_RUST = 0
HAVE_OPENGL = 0
HAVE_JIT = 0
HAVE_CDROM_NEW = 0

CORE_DIR := .
HAVE_GRIFFIN = 0

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
else ifneq (,$(findstring armv,$(platform)))
   override platform += unix
endif

ifneq ($(platform), osx)
   ifeq ($(findstring Haiku,$(shell uname -a)),)
      PTHREAD_FLAGS = -lpthread
   endif
endif

NEED_CD = 1
NEED_TREMOR = 1
NEED_BPP = 32
WANT_NEW_API = 1
NEED_DEINTERLACER = 1
NEED_THREADING = 1
CORE_DEFINE := -DWANT_PSX_EMU
TARGET_NAME := mednafen_psx

ifeq ($(HAVE_OPENGL),1)
   TARGET_NAME := mednafen_psx_hw
endif

# Unix
ifneq (,$(findstring unix,$(platform)))
   TARGET := $(TARGET_NAME)_libretro.so
   fpic := -fPIC
   SHARED := -shared -Wl,--no-undefined -Wl,--version-script=link.T
   ifneq ($(shell uname -p | grep -E '((i.|x)86|amd64)'),)
      IS_X86 = 1
   endif
   LDFLAGS += $(PTHREAD_FLAGS)
   FLAGS += $(PTHREAD_FLAGS) -DHAVE_MKDIR
   ifeq ($(HAVE_OPENGL),1)
      ifneq (,$(findstring gles,$(platform)))
         GLES = 1
         GL_LIB := -lGLESv2
      else
         GL_LIB := -lGL
      endif
   endif

# OS X
else ifeq ($(platform), osx)
   TARGET := $(TARGET_NAME)_libretro.dylib
   fpic := -fPIC
   SHARED := -dynamiclib
   LDFLAGS += $(PTHREAD_FLAGS)
   FLAGS += $(PTHREAD_FLAGS) -DHAVE_MKDIR
   ifeq ($(arch),ppc)
      ENDIANNESS_DEFINES := -DMSB_FIRST
      OLD_GCC := 1
   endif
   OSXVER = `sw_vers -productVersion | cut -d. -f 2`
   OSX_LT_MAVERICKS = `(( $(OSXVER) <= 9)) && echo "YES"`
   ifeq ($(OSX_LT_MAVERICKS),"YES")
      fpic += -mmacosx-version-min=10.5
   endif
   ifeq ($(HAVE_OPENGL),1)
      GL_LIB := -framework OpenGL
   endif

# iOS
else ifneq (,$(findstring ios,$(platform)))
   TARGET := $(TARGET_NAME)_libretro_ios.dylib
   fpic := -fPIC
   SHARED := -dynamiclib
   LDFLAGS += $(PTHREAD_FLAGS)
   FLAGS += $(PTHREAD_FLAGS)
   ifeq ($(IOSSDK),)
      IOSSDK := $(shell xcrun -sdk iphoneos -show-sdk-path)
   endif
   ifeq ($(HAVE_OPENGL),1)
      GL_LIB := -framework OpenGLES
   endif
   CC = cc -arch armv7 -isysroot $(IOSSDK)
   CXX = c++ -arch armv7 -isysroot $(IOSSDK)
   IPHONEMINVER :=
   ifeq ($(platform),ios9)
      IPHONEMINVER = -miphoneos-version-min=8.0
   else
      IPHONEMINVER = -miphoneos-version-min=5.0
   endif
   LDFLAGS += $(IPHONEMINVER)
   FLAGS += $(IPHONEMINVER)
   CC += $(IPHONEMINVER)
   CXX += $(IPHONEMINVER)

# QNX
else ifeq ($(platform), qnx)
   TARGET := $(TARGET_NAME)_libretro_$(platform).so
   fpic := -fPIC
   SHARED := -lcpp -lm -shared -Wl,--no-undefined -Wl,--version-script=link.T
   #LDFLAGS += $(PTHREAD_FLAGS)
   #FLAGS += $(PTHREAD_FLAGS) -DHAVE_MKDIR
   FLAGS += -DHAVE_MKDIR
   CC = qcc -Vgcc_ntoarmv7le
   CXX = QCC -Vgcc_ntoarmv7le_cpp
   AR = QCC -Vgcc_ntoarmv7le
   FLAGS += -D__BLACKBERRY_QNX__ -marm -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=softfp
   ifeq ($(HAVE_OPENGL),1)
      GL_LIB := -lGLESv2
   endif

# PS3
else ifeq ($(platform), ps3)
   TARGET := $(TARGET_NAME)_libretro_$(platform).a
   CC = $(CELL_SDK)/host-win32/ppu/bin/ppu-lv2-gcc.exe
   CXX = $(CELL_SDK)/host-win32/ppu/bin/ppu-lv2-g++.exe
   AR = $(CELL_SDK)/host-win32/ppu/bin/ppu-lv2-ar.exe
   ENDIANNESS_DEFINES := -DMSB_FIRST
   OLD_GCC := 1
   FLAGS += -DHAVE_MKDIR -DARCH_POWERPC_ALTIVEC
   STATIC_LINKING = 1

# sncps3
else ifeq ($(platform), sncps3)
   TARGET := $(TARGET_NAME)_libretro_ps3.a
   CC = $(CELL_SDK)/host-win32/sn/bin/ps3ppusnc.exe
   CXX = $(CELL_SDK)/host-win32/sn/bin/ps3ppusnc.exe
   AR = $(CELL_SDK)/host-win32/sn/bin/ps3snarl.exe
   ENDIANNESS_DEFINES := -DMSB_FIRST
   CXXFLAGS += -Xc+=exceptions
   OLD_GCC := 1
   NO_GCC := 1
   FLAGS += -DHAVE_MKDIR -DARCH_POWERPC_ALTIVEC
   STATIC_LINKING = 1

# Lightweight PS3 Homebrew SDK
else ifeq ($(platform), psl1ght)
   TARGET := $(TARGET_NAME)_libretro_$(platform).a
   CC = $(PS3DEV)/ppu/bin/ppu-gcc$(EXE_EXT)
   CXX = $(PS3DEV)/ppu/bin/ppu-g++$(EXE_EXT)
   AR = $(PS3DEV)/ppu/bin/ppu-ar$(EXE_EXT)
   ENDIANNESS_DEFINES := -DMSB_FIRST
   FLAGS += -DHAVE_MKDIR 
   STATIC_LINKING = 1

# PSP
else ifeq ($(platform), psp1)
   TARGET := $(TARGET_NAME)_libretro_$(platform).a
   CC = psp-gcc$(EXE_EXT)
   CXX = psp-g++$(EXE_EXT)
   AR = psp-ar$(EXE_EXT)
   FLAGS += -DPSP -G0
   FLAGS += -DHAVE_MKDIR
   STATIC_LINKING = 1
   EXTRA_INCLUDES := -I$(shell psp-config --pspsdk-path)/include

# Vita
else ifeq ($(platform), vita)
   TARGET := $(TARGET_NAME)_libretro_$(platform).a
   CC = arm-vita-eabi-gcc$(EXE_EXT)
   CXX = arm-vita-eabi-g++$(EXE_EXT)
   AR = arm-vita-eabi-ar$(EXE_EXT)
   FLAGS += -DVITA
   FLAGS += -DHAVE_MKDIR
   STATIC_LINKING = 1

# Xbox 360
else ifeq ($(platform), xenon)
   TARGET := $(TARGET_NAME)_libretro_xenon360.a
   CC = xenon-gcc$(EXE_EXT)
   CXX = xenon-g++$(EXE_EXT)
   AR = xenon-ar$(EXE_EXT)
   ENDIANNESS_DEFINES += -D__LIBXENON__ -m32 -D__ppc__ -DMSB_FIRST 
   LIBS := $(PTHREAD_FLAGS)
   FLAGS += -DHAVE_MKDIR
   STATIC_LINKING = 1

# Nintendo Game Cube / Nintendo Wii
else ifneq (,$(filter $(platform),ngc wii))
   ifeq ($(platform), ngc)
      TARGET := $(TARGET_NAME)_libretro_$(platform).a
      ENDIANNESS_DEFINES += -DHW_DOL
   else ifeq ($(platform), wii)
      TARGET := $(TARGET_NAME)_libretro_$(platform).a
      ENDIANNESS_DEFINES += -DHW_RVL
   endif
   ENDIANNESS_DEFINES += -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float -DMSB_FIRST 
   CC = $(DEVKITPPC)/bin/powerpc-eabi-gcc$(EXE_EXT)
   CXX = $(DEVKITPPC)/bin/powerpc-eabi-g++$(EXE_EXT)
   AR = $(DEVKITPPC)/bin/powerpc-eabi-ar$(EXE_EXT)
   EXTRA_INCLUDES := -I$(DEVKITPRO)/libogc/include
   FLAGS += -DHAVE_MKDIR
   STATIC_LINKING = 1

# GCW0
else ifeq ($(platform), gcw0)
   TARGET := $(TARGET_NAME)_libretro.so
   CC = /opt/gcw0-toolchain/usr/bin/mipsel-linux-gcc
   CXX = /opt/gcw0-toolchain/usr/bin/mipsel-linux-g++
   AR = /opt/gcw0-toolchain/usr/bin/mipsel-linux-ar
   fpic := -fPIC
   SHARED := -shared -Wl,--no-undefined -Wl,--version-script=link.T
   LDFLAGS += $(PTHREAD_FLAGS)
   FLAGS += $(PTHREAD_FLAGS) -DHAVE_MKDIR
   FLAGS += -ffast-math -march=mips32 -mtune=mips32r2 -mhard-float
   GLES = 1
   GL_LIB := -lGLESv2
   
# Emscripten
else ifeq ($(platform), emscripten)
   TARGET := $(TARGET_NAME)_libretro_$(platform).bc
   fpic := -fPIC
   SHARED := -shared -Wl,--no-undefined -Wl,--version-script=link.T
   LDFLAGS += $(PTHREAD_FLAGS)
   FLAGS += $(PTHREAD_FLAGS) -DHAVE_MKDIR -Dretro_fopen=gg_retro_fopen\
                 -Dmain=gg_main\
                 -Dretro_fclose=gg_retro_fclose\
                 -Dretro_fseek=gg_retro_fseek\
                 -Dretro_fread=gg_retro_fread\
                 -Dretro_fwrite=gg_retro_fwrite\
                 -Dpath_is_directory=gg_path_is_directory\
                 -Dscond_broadcast=gg_scond_broadcast\
                 -Dscond_wait_timeout=gg_scond_wait_timeout\
                 -Dscond_signal=gg_scond_signal\
                 -Dscond_wait=gg_scond_wait\
                 -Dscond_free=gg_scond_free\
                 -Dscond_new=gg_scond_new\
                 -Dslock_unlock=gg_slock_unlock\
                 -Dslock_lock=gg_slock_lock\
                 -Dslock_free=gg_slock_free\
                 -Dslock_new=gg_slock_new\
                 -Dsthread_join=gg_sthread_join\
                 -Dsthread_detach=gg_sthread_detach\
                 -Dsthread_create=gg_sthread_create\
                 -Dscond=gg_scond\
                 -Dslock=gg_slock\
                 -Drglgen_symbol_map=mupen_rglgen_symbol_map \
		 -Dmain_exit=mupen_main_exit \
		 -Dadler32=mupen_adler32 \
		 -Drglgen_resolve_symbols_custom=mupen_rglgen_resolve_symbols_custom \
		 -Drglgen_resolve_symbols=mupen_rglgen_resolve_symbols \
		 -Dsinc_resampler=mupen_sinc_resampler \
		 -Dnearest_resampler=mupen_nearest_resampler \
		 -DCC_resampler=mupen_CC_resampler \
		 -Daudio_resampler_driver_find_handle=mupen_audio_resampler_driver_find_handle \
		 -Daudio_resampler_driver_find_ident=mupen_audio_resampler_driver_find_ident \
		 -Drarch_resampler_realloc=mupen_rarch_resampler_realloc \
		 -Daudio_convert_s16_to_float_C=mupen_audio_convert_s16_to_float_C \
		 -Daudio_convert_float_to_s16_C=mupen_audio_convert_float_to_s16_C \
		 -Daudio_convert_init_simd=mupen_audio_convert_init_simd \
                 -Dfilestream_open=gg_filestream_open\
                 -Dfilestream_get_fd=gg_filestream_get_fd\
                 -Dfilestream_read=gg_filestream_read\
                 -Dfilestream_seek=gg_filestream_seek\
                 -Dfilestream_close=gg_filestream_close\
                 -Dfilestream_tell=gg_filestream_tell\
                 -Dfilestream_read_file=gg_filestream_read_file\
                 -Dfilestream_write_file=gg_filestream_write_file\
                 -Dfilestream_write=gg_filestream_write\
                 -Dfilestream_rewind=gg_filestream_rewind\
                 -Dfilestream_putc=gg_filestream_putc\
                 -Dpath_is_character_special=gg_path_is_character_special\
                 -Dpath_is_valid=gg_path_is_valid\
                 -Dpath_is_compressed=gg_path_is_compressed\
                 -Dpath_is_compressed_file=gg_path_is_compressed_file\
                 -Dpath_is_absolute=gg_path_is_absolute\
                 -Dpath_is_directory=gg_path_is_directory\
                 -Dpath_get_size=gg_path_get_size\
                 -Dpath_get_extension=gg_path_get_extension\
                 -Dstring_is_empty=gg_string_is_empty\
                 -Dstring_is_equal=gg_string_is_equal\
                 -Dstring_to_upper=gg_string_to_upper\
                 -Dstring_to_lower=gg_string_to_lower\
                 -Dstring_ucwords=gg_string_ucwords\
                 -Dstring_replace_substring=gg_string_replace_substring\
                 -Dstring_trim_whitespace_left=gg_string_trim_whitespace_left\
                 -Dstring_trim_whitespace_right=gg_string_trim_whitespace_right\
                 -Dstring_trim_whitespace_left=gg_string_trim_whitespace_left\
                 -Dstring_trim_whitespace=gg_string_trim_whitespace\
                 -Dsthread_isself=gg_sthread_isself\
                 -Dstring_is_equal_noncase=gg_string_is_equal_noncase\
                 -Dmkdir_norecurse=gg_mkdir_norecurse

ifeq ($(HAVE_OPENGL),1)
	ifneq (,$(findstring gles,$(platform)))
		GLES = 1
		GL_LIB := -lGLESv2
	else
		GL_LIB := -lGL
	endif
endif


# Windows
else
   TARGET := $(TARGET_NAME)_libretro.dll
   CC = gcc
   CXX = g++
   IS_X86 = 1
   SHARED := -shared -Wl,--no-undefined -Wl,--version-script=link.T
   LDFLAGS += -static-libgcc -static-libstdc++ -lwinmm
   FLAGS += -DHAVE__MKDIR
   ifeq ($(HAVE_OPENGL),1)
      GL_LIB := -lopengl32
   endif

endif

include Makefile.common

WARNINGS := -Wall \
   -Wno-sign-compare \
   -Wno-unused-variable \
   -Wno-unused-function \
   -Wno-uninitialized \
   $(NEW_GCC_WARNING_FLAGS) \
   -Wno-strict-aliasing

#EXTRA_GCC_FLAGS := -funroll-loops

ifeq ($(NO_GCC),1)
   WARNINGS :=
endif

OBJECTS := $(SOURCES_CXX:.cpp=.o) $(SOURCES_C:.c=.o)

all: $(TARGET)

ifeq ($(DEBUG),0)
   FLAGS += -O2 $(EXTRA_GCC_FLAGS)
else
   FLAGS += -O0 -g
endif

LDFLAGS += $(fpic) $(SHARED)
FLAGS += $(fpic) $(NEW_GCC_FLAGS)
FLAGS += $(INCFLAGS)

FLAGS += $(ENDIANNESS_DEFINES) -DSIZEOF_DOUBLE=8 $(WARNINGS) -DMEDNAFEN_VERSION=\"0.9.38.6\" -DPACKAGE=\"mednafen\" -DMEDNAFEN_VERSION_NUMERIC=9386 -DPSS_STYLE=1 -DMPC_FIXED_POINT $(CORE_DEFINE) -DSTDC_HEADERS -D__STDC_LIMIT_MACROS -D__LIBRETRO__ -D_LOW_ACCURACY_ $(EXTRA_INCLUDES) $(SOUND_DEFINE) -D__STDC_CONSTANT_MACROS

ifeq ($(HAVE_RUST),1)
   FLAGS += -DHAVE_RUST
   LDFLAGS += -ldl -L. -lrsx
endif

ifeq ($(HAVE_JIT),1)
   LDFLAGS += -ljit
endif

CXXFLAGS += $(FLAGS)
CFLAGS   += $(FLAGS)

$(TARGET): $(OBJECTS)
ifeq ($(STATIC_LINKING), 1)
	$(AR) rcs $@ $(OBJECTS)
else
	$(CXX) -o $@ $^ $(LDFLAGS) $(GL_LIB)
endif

%.o: %.cpp
	$(CXX) -c -o $@ $< $(CXXFLAGS)

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

clean:
	rm -f $(TARGET) $(OBJECTS)

.PHONY: clean

