DEBUG = 0
FRONTEND_SUPPORTS_RGB565 = 1
HAVE_OPENGL = 0
HAVE_VULKAN = 0
HAVE_JIT = 0
HAVE_CHD = 1

CORE_DIR := .
HAVE_GRIFFIN = 0

SPACE :=
SPACE := $(SPACE) $(SPACE)
BACKSLASH :=
BACKSLASH := \$(BACKSLASH)
filter_out1 = $(filter-out $(firstword $1),$1)
filter_out2 = $(call filter_out1,$(call filter_out1,$1))

GIT_VERSION ?= " $(shell git rev-parse --short HEAD || echo unknown)"
ifneq ($(GIT_VERSION)," unknown")
   FLAGS += -DGIT_VERSION=\"$(GIT_VERSION)\"
endif

ifeq ($(platform),)
   platform = unix
   ifeq ($(shell uname -a),)
      platform = win
   else ifneq ($(findstring Darwin,$(shell uname -a)),)
      platform = osx
      arch     = intel
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
SET_HAVE_HW = 0
CORE_DEFINE := -DWANT_PSX_EMU
TARGET_NAME := mednafen_psx

ifeq ($(HAVE_HW), 1)
   HAVE_VULKAN = 1
   HAVE_OPENGL = 1
   SET_HAVE_HW = 1
endif

ifeq ($(VULKAN), 1)
   SET_HAVE_HW = 1
endif

ifeq ($(HAVE_OPENGL), 1)
   SET_HAVE_HW = 1
endif

ifeq ($(SET_HAVE_HW), 1)
   FLAGS += -DHAVE_HW
   TARGET_NAME := mednafen_psx_hw
endif


# Unix
ifneq (,$(findstring unix,$(platform)))
   TARGET := $(TARGET_NAME)_libretro.so
   fpic   := -fPIC
   SHARED := -shared -Wl,--no-undefined -Wl,--version-script=link.T
   ifneq ($(shell uname -p | grep -E '((i.|x)86|amd64)'),)
      IS_X86 = 1
   endif
   LDFLAGS += $(PTHREAD_FLAGS)
   FLAGS   +=
   ifeq ($(HAVE_OPENGL),1)
      ifneq (,$(findstring gles,$(platform)))
         GLES = 1
         GL_LIB := -lGLESv2
      else
         GL_LIB := -L/usr/local/lib -lGL
      endif
   endif

# OS X
else ifeq ($(platform), osx)
   TARGET  := $(TARGET_NAME)_libretro.dylib
   fpic    := -fPIC
   SHARED  := -dynamiclib
   LDFLAGS += $(PTHREAD_FLAGS)
   FLAGS   += $(PTHREAD_FLAGS)
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
   TARGET  := $(TARGET_NAME)_libretro_ios.dylib
   fpic    := -fPIC
   SHARED  := -dynamiclib
   LDFLAGS += $(PTHREAD_FLAGS)
   FLAGS   += $(PTHREAD_FLAGS)
   ifeq ($(IOSSDK),)
      IOSSDK := $(shell xcrun -sdk iphoneos -show-sdk-path)
   endif
   ifeq ($(HAVE_OPENGL),1)
      GL_LIB := -framework OpenGLES
   endif
   CC = cc -arch armv7 -isysroot $(IOSSDK)
   CXX = c++ -arch armv7 -isysroot $(IOSSDK)
   IPHONEMINVER :=
   ifeq ($(platform),$(filter $(platform),ios9 ios-arm64))
      IPHONEMINVER = -miphoneos-version-min=8.0
   else
      IPHONEMINVER = -miphoneos-version-min=5.0
   endif
   LDFLAGS += $(IPHONEMINVER)
   FLAGS   += $(IPHONEMINVER)
   CC      += $(IPHONEMINVER)
   CXX     += $(IPHONEMINVER)

# QNX
else ifeq ($(platform), qnx)
   TARGET := $(TARGET_NAME)_libretro_$(platform).so
   fpic   := -fPIC
   SHARED := -lcpp -lm -shared -Wl,--no-undefined -Wl,--version-script=link.T
   #LDFLAGS += $(PTHREAD_FLAGS)
   CC     = qcc -Vgcc_ntoarmv7le
   CXX    = QCC -Vgcc_ntoarmv7le_cpp
   AR     = QCC -Vgcc_ntoarmv7le
   FLAGS += -D__BLACKBERRY_QNX__ -marm -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=softfp
   ifeq ($(HAVE_OPENGL),1)
      GL_LIB := -lGLESv2
   endif

# PS3
else ifeq ($(platform), ps3)
   TARGET := $(TARGET_NAME)_libretro_$(platform).a
   CC      = $(CELL_SDK)/host-win32/ppu/bin/ppu-lv2-gcc.exe
   CXX     = $(CELL_SDK)/host-win32/ppu/bin/ppu-lv2-g++.exe
   AR      = $(CELL_SDK)/host-win32/ppu/bin/ppu-lv2-ar.exe
   ENDIANNESS_DEFINES := -DMSB_FIRST
   OLD_GCC := 1
   FLAGS += -DARCH_POWERPC_ALTIVEC
   STATIC_LINKING = 1

# sncps3
else ifeq ($(platform), sncps3)
   TARGET := $(TARGET_NAME)_libretro_ps3.a
   CC      = $(CELL_SDK)/host-win32/sn/bin/ps3ppusnc.exe
   CXX     = $(CELL_SDK)/host-win32/sn/bin/ps3ppusnc.exe
   AR      = $(CELL_SDK)/host-win32/sn/bin/ps3snarl.exe
   ENDIANNESS_DEFINES := -DMSB_FIRST
   CXXFLAGS += -Xc+=exceptions
   OLD_GCC  := 1
   NO_GCC   := 1
   FLAGS    += -DARCH_POWERPC_ALTIVEC
   STATIC_LINKING = 1

# Lightweight PS3 Homebrew SDK
else ifeq ($(platform), psl1ght)
   TARGET := $(TARGET_NAME)_libretro_$(platform).a
   CC      = $(PS3DEV)/ppu/bin/ppu-gcc$(EXE_EXT)
   CXX     = $(PS3DEV)/ppu/bin/ppu-g++$(EXE_EXT)
   AR      = $(PS3DEV)/ppu/bin/ppu-ar$(EXE_EXT)
   ENDIANNESS_DEFINES := -DMSB_FIRST
   STATIC_LINKING = 1

# PSP
else ifeq ($(platform), psp1)
   TARGET := $(TARGET_NAME)_libretro_$(platform).a
   CC      = psp-gcc$(EXE_EXT)
   CXX     = psp-g++$(EXE_EXT)
   AR      = psp-ar$(EXE_EXT)
   FLAGS  += -DPSP -G0
   STATIC_LINKING = 1
   EXTRA_INCLUDES := -I$(shell psp-config --pspsdk-path)/include

# Vita
else ifeq ($(platform), vita)
   TARGET := $(TARGET_NAME)_libretro_$(platform).a
   CC      = arm-vita-eabi-gcc$(EXE_EXT)
   CXX     = arm-vita-eabi-g++$(EXE_EXT)
   AR      = arm-vita-eabi-ar$(EXE_EXT)
   FLAGS  += -DVITA
   STATIC_LINKING = 1

# Xbox 360
else ifeq ($(platform), xenon)
   TARGET := $(TARGET_NAME)_libretro_xenon360.a
   CC      = xenon-gcc$(EXE_EXT)
   CXX     = xenon-g++$(EXE_EXT)
   AR      = xenon-ar$(EXE_EXT)
   ENDIANNESS_DEFINES += -D__LIBXENON__ -m32 -D__ppc__ -DMSB_FIRST
   LIBS := $(PTHREAD_FLAGS)
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
   CC  = $(DEVKITPPC)/bin/powerpc-eabi-gcc$(EXE_EXT)
   CXX = $(DEVKITPPC)/bin/powerpc-eabi-g++$(EXE_EXT)
   AR  = $(DEVKITPPC)/bin/powerpc-eabi-ar$(EXE_EXT)
   EXTRA_INCLUDES := -I$(DEVKITPRO)/libogc/include
   STATIC_LINKING = 1

# Nintendo WiiU
else ifeq ($(platform), wiiu)
   TARGET := $(TARGET_NAME)_libretro_$(platform).a
   CC      = $(DEVKITPPC)/bin/powerpc-eabi-gcc$(EXE_EXT)
   CXX     = $(DEVKITPPC)/bin/powerpc-eabi-g++$(EXE_EXT)
   AR      = $(DEVKITPPC)/bin/powerpc-eabi-ar$(EXE_EXT)
   FLAGS  += -DGEKKO -mwup -mcpu=750 -meabi -mhard-float
   FLAGS  += -U__INT32_TYPE__ -U __UINT32_TYPE__ -D__INT32_TYPE__=int
   ENDIANNESS_DEFINES += -DMSB_FIRST
   EXTRA_INCLUDES     := -Ideps
   STATIC_LINKING = 1
   NEED_THREADING = 0

# GCW0
else ifeq ($(platform), gcw0)
   TARGET  := $(TARGET_NAME)_libretro.so
   CC       = /opt/gcw0-toolchain/usr/bin/mipsel-linux-gcc
   CXX      = /opt/gcw0-toolchain/usr/bin/mipsel-linux-g++
   AR       = /opt/gcw0-toolchain/usr/bin/mipsel-linux-ar
   fpic    := -fPIC
   SHARED  := -shared -Wl,--no-undefined -Wl,--version-script=link.T
   LDFLAGS += $(PTHREAD_FLAGS)
   FLAGS   += $(PTHREAD_FLAGS)
   FLAGS   += -ffast-math -march=mips32 -mtune=mips32r2 -mhard-float
   GLES     = 1
   GL_LIB  := -lGLESv2

# Emscripten
else ifeq ($(platform), emscripten)
   TARGET  := $(TARGET_NAME)_libretro_$(platform).bc
   fpic    := -fPIC
   SHARED  := -shared -Wl,--no-undefined -Wl,--version-script=link.T
   LDFLAGS += $(PTHREAD_FLAGS)
   FLAGS   += $(PTHREAD_FLAGS) -Dretro_fopen=gg_retro_fopen\
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
               -Dfilestream_set_size=gg_filestream_set_size\
               -Dfilestream_get_ext=gg_filestream_get_ext\
               -Dfilestream_get_size=gg_filestream_get_size\
               -Dfilestream_read_file=gg_filestream_read_file\
               -Dfilestream_write_file=gg_filestream_write_file\
               -Dfilestream_write=gg_filestream_write\
               -Dfilestream_rewind=gg_filestream_rewind\
               -Dfilestream_putc=gg_filestream_putc\
               -Dfilestream_getline=gg_filestream_getline\
               -Dfilestream_getc=gg_filestream_getc\
               -Dfilestream_gets=gg_filestream_gets\
               -Dfilestream_eof=gg_filestream_eof\
               -Dfilestream_flush=gg_filestream_flush\
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

   STATIC_LINKING = 1

   ifeq ($(HAVE_OPENGL),1)
      ifneq (,$(findstring gles,$(platform)))
         GLES = 1
         GL_LIB := -lGLESv2
      else
         GL_LIB := -lGL
      endif
   endif

# Windows MSVC 2017 all architectures
else ifneq (,$(findstring windows_msvc2017,$(platform)))

    NO_GCC := 1

	PlatformSuffix = $(subst windows_msvc2017_,,$(platform))
	ifneq (,$(findstring desktop,$(PlatformSuffix)))
		WinPartition = desktop
		MSVC2017CompileFlags = -DWINAPI_FAMILY=WINAPI_FAMILY_DESKTOP_APP -FS
		LDFLAGS += -MANIFEST -LTCG:incremental -NXCOMPAT -DYNAMICBASE -DEBUG -OPT:REF -INCREMENTAL:NO -SUBSYSTEM:WINDOWS -MANIFESTUAC:"level='asInvoker' uiAccess='false'" -OPT:ICF -ERRORREPORT:PROMPT -NOLOGO -TLBID:1
		LIBS += kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib
	else ifneq (,$(findstring uwp,$(PlatformSuffix)))
		WinPartition = uwp
		MSVC2017CompileFlags = -DWINAPI_FAMILY=WINAPI_FAMILY_APP -D_WINDLL -D_UNICODE -DUNICODE -D__WRL_NO_DEFAULT_LIB__ -EHsc -FS
		LDFLAGS += -APPCONTAINER -NXCOMPAT -DYNAMICBASE -MANIFEST:NO -LTCG -OPT:REF -SUBSYSTEM:CONSOLE -MANIFESTUAC:NO -OPT:ICF -ERRORREPORT:PROMPT -NOLOGO -TLBID:1 -DEBUG:FULL -WINMD:NO
		LIBS += WindowsApp.lib
	endif

	CFLAGS += $(MSVC2017CompileFlags)
	CXXFLAGS += $(MSVC2017CompileFlags)

	TargetArchMoniker = $(subst $(WinPartition)_,,$(PlatformSuffix))

	CC  = cl.exe
	CXX = cl.exe
	LD = link.exe

	reg_query = $(call filter_out2,$(subst $2,,$(shell reg query "$2" -v "$1" 2>nul)))
	fix_path = $(subst $(SPACE),\ ,$(subst \,/,$1))

	ProgramFiles86w := $(shell cmd /c "echo %PROGRAMFILES(x86)%")
	ProgramFiles86 := $(shell cygpath "$(ProgramFiles86w)")

	WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_LOCAL_MACHINE\SOFTWARE\Wow6432Node\Microsoft\Microsoft SDKs\Windows\v10.0)
	WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_CURRENT_USER\SOFTWARE\Wow6432Node\Microsoft\Microsoft SDKs\Windows\v10.0)
	WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v10.0)
	WindowsSdkDir ?= $(call reg_query,InstallationFolder,HKEY_CURRENT_USER\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v10.0)
	WindowsSdkDir := $(WindowsSdkDir)

	WindowsSDKVersion ?= $(firstword $(foreach folder,$(subst $(subst \,/,$(WindowsSdkDir)Include/),,$(wildcard $(call fix_path,$(WindowsSdkDir)Include\*))),$(if $(wildcard $(call fix_path,$(WindowsSdkDir)Include/$(folder)/um/Windows.h)),$(folder),)))$(BACKSLASH)
	WindowsSDKVersion := $(WindowsSDKVersion)

	VsInstallBuildTools = $(ProgramFiles86)/Microsoft Visual Studio/2017/BuildTools
	VsInstallEnterprise = $(ProgramFiles86)/Microsoft Visual Studio/2017/Enterprise
	VsInstallProfessional = $(ProgramFiles86)/Microsoft Visual Studio/2017/Professional
	VsInstallCommunity = $(ProgramFiles86)/Microsoft Visual Studio/2017/Community

	VsInstallRoot ?= $(shell if [ -d "$(VsInstallBuildTools)" ]; then echo "$(VsInstallBuildTools)"; fi)
	ifeq ($(VsInstallRoot), )
		VsInstallRoot = $(shell if [ -d "$(VsInstallEnterprise)" ]; then echo "$(VsInstallEnterprise)"; fi)
	endif
	ifeq ($(VsInstallRoot), )
		VsInstallRoot = $(shell if [ -d "$(VsInstallProfessional)" ]; then echo "$(VsInstallProfessional)"; fi)
	endif
	ifeq ($(VsInstallRoot), )
		VsInstallRoot = $(shell if [ -d "$(VsInstallCommunity)" ]; then echo "$(VsInstallCommunity)"; fi)
	endif
	VsInstallRoot := $(VsInstallRoot)

	VcCompilerToolsVer := $(shell cat "$(VsInstallRoot)/VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt" | grep -o '[0-9\.]*')
	VcCompilerToolsDir := $(VsInstallRoot)/VC/Tools/MSVC/$(VcCompilerToolsVer)

	WindowsSDKSharedIncludeDir := $(shell cygpath -w "$(WindowsSdkDir)\Include\$(WindowsSDKVersion)\shared")
	WindowsSDKUCRTIncludeDir := $(shell cygpath -w "$(WindowsSdkDir)\Include\$(WindowsSDKVersion)\ucrt")
	WindowsSDKUMIncludeDir := $(shell cygpath -w "$(WindowsSdkDir)\Include\$(WindowsSDKVersion)\um")
	WindowsSDKUCRTLibDir := $(shell cygpath -w "$(WindowsSdkDir)\Lib\$(WindowsSDKVersion)\ucrt\$(TargetArchMoniker)")
	WindowsSDKUMLibDir := $(shell cygpath -w "$(WindowsSdkDir)\Lib\$(WindowsSDKVersion)\um\$(TargetArchMoniker)")

	# For some reason the HostX86 compiler doesn't like compiling for x64
	# ("no such file" opening a shared library), and vice-versa.
	# Work around it for now by using the strictly x86 compiler for x86, and x64 for x64.
	# NOTE: What about ARM?
	ifneq (,$(findstring x64,$(TargetArchMoniker)))
		VCCompilerToolsBinDir := $(VcCompilerToolsDir)\bin\HostX64
	else
		VCCompilerToolsBinDir := $(VcCompilerToolsDir)\bin\HostX86
	endif

	PATH := $(shell IFS=$$'\n'; cygpath "$(VCCompilerToolsBinDir)/$(TargetArchMoniker)"):$(PATH)
	PATH := $(PATH):$(shell IFS=$$'\n'; cygpath "$(VsInstallRoot)/Common7/IDE")
	INCLUDE := $(shell IFS=$$'\n'; cygpath -w "$(VcCompilerToolsDir)/include")
	LIB := $(shell IFS=$$'\n'; cygpath -w "$(VcCompilerToolsDir)/lib/$(TargetArchMoniker)")
	ifneq (,$(findstring uwp,$(PlatformSuffix)))
		LIB := $(shell IFS=$$'\n'; cygpath -w "$(LIB)/store")
	endif
    
	export INCLUDE := $(INCLUDE);$(WindowsSDKSharedIncludeDir);$(WindowsSDKUCRTIncludeDir);$(WindowsSDKUMIncludeDir)
	export LIB := $(LIB);$(WindowsSDKUCRTLibDir);$(WindowsSDKUMLibDir)
	TARGET := $(TARGET_NAME)_libretro.dll $(TARGET_NAME)_libretro.lib $(TARGET_NAME)_libretro.pdb $(TARGET_NAME)_libretro.exp
	PSS_STYLE :=2
	LDFLAGS += -DLL

# Windows
else
   TARGET  := $(TARGET_NAME)_libretro.dll
   CC       = gcc
   CXX      = g++
   IS_X86   = 1
   SHARED  := -shared -Wl,--no-undefined -Wl,--version-script=link.T
   LDFLAGS += -static-libgcc -static-libstdc++ -lwinmm
   FLAGS   += -DHAVE__MKDIR
   
   ifeq ($(HAVE_OPENGL),1)
      GL_LIB := -lopengl32
   endif
endif

include Makefile.common

# https://github.com/libretro-mirrors/mednafen-git/blob/master/README.PORTING
MEDNAFEN_GCC_FLAGS = -fwrapv \
                     -fsigned-char
                     
ifeq ($(IS_X86),1)
   MEDNAFEN_GCC_FLAGS += -fomit-frame-pointer
endif

WARNINGS := -Wall \
            -Wno-sign-compare \
            -Wno-unused-variable \
            -Wno-unused-function \
            -Wno-uninitialized \
            $(NEW_GCC_WARNING_FLAGS) \
            -Wno-strict-aliasing

ifeq ($(NO_GCC),1)
   WARNINGS :=
else
   FLAGS += $(MEDNAFEN_GCC_FLAGS)
endif

OBJECTS := $(SOURCES_CXX:.cpp=.o) $(SOURCES_C:.c=.o)
DEPS    := $(SOURCES_CXX:.cpp=.d) $(SOURCES_C:.c=.d)

all: $(TARGET)

-include $(DEPS)

ifeq ($(DEBUG), 1)
ifneq (,$(findstring msvc,$(platform)))
	ifeq ($(STATIC_LINKING),1)
	CFLAGS += -MTd
	CXXFLAGS += -MTd
else
	CFLAGS += -MDd
	CXXFLAGS += -MDd
endif

CFLAGS += -Od -Zi -DDEBUG -D_DEBUG
CXXFLAGS += -Od -Zi -DDEBUG -D_DEBUG
	else
	CFLAGS += -O0 -g -DDEBUG
	CXXFLAGS += -O0 -g -DDEBUG
endif
else
ifneq (,$(findstring msvc,$(platform)))
ifeq ($(STATIC_LINKING),1)
	CFLAGS += -MT
	CXXFLAGS += -MT
else
	CFLAGS += -MD
	CXXFLAGS += -MD
endif

CFLAGS += -O2 -DNDEBUG
CXXFLAGS += -O2 -DNDEBUG
else
	CFLAGS += -O3 -DNDEBUG
	CXXFLAGS += -O3 -DNDEBUG
endif
endif

LDFLAGS += $(fpic) $(SHARED)
FLAGS   += $(fpic) $(NEW_GCC_FLAGS)
FLAGS   += $(INCFLAGS)

FLAGS += $(ENDIANNESS_DEFINES) \
         -DSIZEOF_DOUBLE=8 \
         $(WARNINGS) \
         -DMEDNAFEN_VERSION=\"0.9.38.6\" \
         -DPACKAGE=\"mednafen\" \
         -DMEDNAFEN_VERSION_NUMERIC=9386 \
         -DPSS_STYLE=1 \
         -DMPC_FIXED_POINT \
         $(CORE_DEFINE) \
         -DSTDC_HEADERS \
         -D__STDC_LIMIT_MACROS \
         -D__LIBRETRO__ \
         -D_LOW_ACCURACY_ \
         $(EXTRA_INCLUDES) \
         $(SOUND_DEFINE) \
         -D_FILE_OFFSET_BITS=64 \
         -D__STDC_CONSTANT_MACROS

ifneq (,$(findstring windows_msvc2017,$(platform)))
FLAGS += -D_CRT_SECURE_NO_WARNINGS \
	 -D_CRT_NONSTDC_NO_DEPRECATE \
	 -D__ORDER_LITTLE_ENDIAN__ \
	 -D__BYTE_ORDER__=__ORDER_LITTLE_ENDIAN__ \
	 -DNOMINMAX \
	 //utf-8 \
	 //std:c++17
	 ifeq (,$(findstring windows_msvc2017_uwp,$(platform)))
	 	 LDFLAGS += opengl32.lib
	 endif
endif

ifeq ($(HAVE_VULKAN),1)
   FLAGS += -DHAVE_VULKAN
endif

ifeq ($(HAVE_JIT),1)
   LDFLAGS += -ljit
endif

CXXFLAGS += $(FLAGS)
CFLAGS   += $(FLAGS)

ifneq ($(SANITIZER),)
    CFLAGS   := -fsanitize=$(SANITIZER) $(CFLAGS)
    CXXFLAGS := -fsanitize=$(SANITIZER) $(CXXFLAGS)
    LDFLAGS  := -fsanitize=$(SANITIZER) $(LDFLAGS)
endif

OBJOUT   = -o
LINKOUT  = -o

ifneq (,$(findstring msvc,$(platform)))
	OBJOUT = -Fo
	LINKOUT = -out:
ifeq ($(STATIC_LINKING),1)
	LD ?= lib.exe
	STATIC_LINKING=0
else
	LD = link.exe
endif
else
	LD = $(CXX)
endif

$(TARGET): $(OBJECTS)
ifeq ($(STATIC_LINKING), 1)
	$(AR) rcs $@ $(OBJECTS)
else
	$(LD) $(LINKOUT)$@ $^ $(LDFLAGS) $(GL_LIB) $(LIBS)
endif

%.o: %.cpp
	$(CXX) -c $(OBJOUT)$@ $< $(CXXFLAGS)

%.o: %.c
	$(CC) -c $(OBJOUT)$@ $< $(CFLAGS)

clean:
	rm -f $(TARGET) $(OBJECTS) $(DEPS)

.PHONY: clean
