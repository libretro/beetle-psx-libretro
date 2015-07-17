LOCAL_PATH := $(call my-dir)
DEBUG = 0
FRONTEND_SUPPORTS_RGB565 = 1
FAST = 1

include $(CLEAR_VARS)

ifeq ($(TARGET_ARCH),arm)
LOCAL_CXXFLAGS += -DANDROID_ARM
LOCAL_CFLAGS +=-DANDROID_ARM
LOCAL_ARM_MODE := arm
endif

ifeq ($(TARGET_ARCH),x86)
LOCAL_CXXFLAGS +=  -DANDROID_X86
LOCAL_CFLAGS += -DANDROID_X86
IS_X86 = 1
endif

ifeq ($(TARGET_ARCH),mips)
LOCAL_CXXFLAGS += -DANDROID_MIPS -D__mips__ -D__MIPSEL__
LOCAL_CFLAGS += -DANDROID_MIPS -D__mips__ -D__MIPSEL__
endif

MEDNAFEN_DIR := ../mednafen
MEDNAFEN_LIBRETRO_DIR := ..

LOCAL_MODULE    := libretro

   PTHREAD_FLAGS = -pthread
   NEED_CD = 1
   NEED_BPP = 32
	WANT_NEW_API = 1
   NEED_DEINTERLACER = 1
	NEED_THREADING = 1
	NEED_TREMOR = 1
   CORE_DEFINE := -DWANT_PSX_EMU
   CORE_DIR := $(MEDNAFEN_DIR)/psx
   CORE_SOURCES := $(CORE_DIR)/irq.cpp \
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

CORE_INCDIR := $(CORE_DIR)

ifeq ($(NEED_THREADING), 1)
FLAGS += -DWANT_THREADING
THREAD_SOURCES += $(MEDNAFEN_LIBRETRO_DIR)/threads.c
endif

ifeq ($(NEED_CRC32), 1)
FLAGS += -DWANT_CRC32
CRC32_SOURCES += $(MEDNAFEN_LIBRETRO_DIR)/scrc32.c
endif

ifeq ($(NEED_DEINTERLACER), 1)
FLAGS += -DNEED_DEINTERLACER
endif

ifeq ($(NEED_CD), 1)
CDROM_SOURCES := $(MEDNAFEN_DIR)/cdrom/CDAccess.cpp $(MEDNAFEN_DIR)/cdrom/CDAccess_Image.cpp $(MEDNAFEN_DIR)/cdrom/CDAccess_CCD.cpp $(MEDNAFEN_DIR)/cdrom/CDUtility.cpp $(MEDNAFEN_DIR)/cdrom/lec.cpp $(MEDNAFEN_DIR)/cdrom/SimpleFIFO.cpp $(MEDNAFEN_DIR)/cdrom/audioreader.cpp $(MEDNAFEN_DIR)/cdrom/galois.cpp $(MEDNAFEN_DIR)/cdrom/recover-raw.cpp $(MEDNAFEN_DIR)/cdrom/l-ec.cpp $(MEDNAFEN_DIR)/cdrom/cdromif.cpp $(MEDNAFEN_DIR)/cdrom/crc32.cpp
FLAGS += -DNEED_CD
endif

ifeq ($(NEED_TREMOR), 1)
TREMOR_SRC := $(wildcard $(MEDNAFEN_DIR)/tremor/*.c)
FLAGS += -DNEED_TREMOR
endif


MEDNAFEN_SOURCES := $(MEDNAFEN_DIR)/error.cpp \
	$(MEDNAFEN_DIR)/math_ops.cpp \
	$(MEDNAFEN_DIR)/settings.cpp \
	$(MEDNAFEN_DIR)/general.cpp \
	$(MEDNAFEN_DIR)/FileWrapper.cpp \
	$(MEDNAFEN_DIR)/FileStream.cpp \
	$(MEDNAFEN_DIR)/MemoryStream.cpp \
	$(MEDNAFEN_DIR)/Stream.cpp \
	$(MEDNAFEN_DIR)/state.cpp \
	$(MEDNAFEN_DIR)/mempatcher.cpp \
	$(MEDNAFEN_DIR)/video/Deinterlacer.cpp \
	$(MEDNAFEN_DIR)/video/surface.cpp \
	$(MEDNAFEN_DIR)/file.cpp \
	$(MEDNAFEN_DIR)/endian.cpp \
	$(MEDNAFEN_DIR)/md5.cpp


LIBRETRO_SOURCES := $(MEDNAFEN_LIBRETRO_DIR)/libretro.cpp

SOURCES_C := 	$(TREMOR_SRC) $(THREAD_SOURCES) $(LIBRETRO_SOURCES_C) $(MEDNAFEN_DIR)/trio/trio.c $(MEDNAFEN_DIR)/trio/triostr.c $(CRC32_SOURCES)

LOCAL_SRC_FILES += $(LIBRETRO_SOURCES) $(CORE_SOURCES) $(MEDNAFEN_SOURCES) $(CDROM_SOURCES) $(SCSI_CD_SOURCES) $(HW_CPU_SOURCES) $(HW_MISC_SOURCES) $(HW_SOUND_SOURCES) $(HW_VIDEO_SOURCES) $(SOURCES_C) $(CORE_CD_SOURCES)

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
endif

ifeq ($(DEBUG),0)
   FLAGS += -O3 $(EXTRA_GCC_FLAGS)
else
   FLAGS += -O0 -g
endif

ifneq ($(OLD_GCC),1)
NEW_GCC_WARNING_FLAGS += -Wno-narrowing \
	-Wno-unused-but-set-variable \
	-Wno-unused-result \
	-Wno-overflow
NEW_GCC_FLAGS += -fno-strict-overflow
endif

LDFLAGS += $(fpic) $(SHARED)
FLAGS += $(fpic) $(NEW_GCC_FLAGS)
LOCAL_C_INCLUDES += .. ../mednafen ../mednafen/include ../mednafen/intl ../mednafen/hw_cpu ../mednafen/hw_sound ../mednafen/hw_misc ../mednafen/hw_video $(CORE_INCDIR) $(EXTRA_CORE_INCDIR)

FLAGS += $(ENDIANNESS_DEFINES) -DSIZEOF_DOUBLE=8 $(WARNINGS) -DMEDNAFEN_VERSION=\"0.9.26\" -DPACKAGE=\"mednafen\" -DMEDNAFEN_VERSION_NUMERIC=926 -DPSS_STYLE=1 -DMPC_FIXED_POINT $(CORE_DEFINE) -DSTDC_HEADERS -D__STDC_LIMIT_MACROS -D__LIBRETRO__ -DNDEBUG -D_LOW_ACCURACY_ $(SOUND_DEFINE) -DLSB_FIRST -D__STDC_CONSTANT_MACROS

ifeq ($(IS_X86), 1)
FLAGS += -DARCH_X86
endif

ifeq ($(NEED_BPP), 16)
FLAGS += -DWANT_16BPP
endif

ifeq ($(WANT_NEW_API), 1)
FLAGS += -DWANT_NEW_API
endif

ifeq ($(FRONTEND_SUPPORTS_RGB565), 1)
FLAGS += -DFRONTEND_SUPPORTS_RGB565
endif

ifeq ($(NEED_BPP), 32)
FLAGS += -DWANT_32BPP
endif

LOCAL_CFLAGS =  $(FLAGS) 
LOCAL_CXXFLAGS = $(FLAGS) -fexceptions

include $(BUILD_SHARED_LIBRARY)
