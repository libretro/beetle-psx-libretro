LOCAL_PATH := $(call my-dir)

CORE_DIR := $(LOCAL_PATH)/..

DEBUG                    := 0
FRONTEND_SUPPORTS_RGB565 := 1
NEED_CD                  := 1
NEED_BPP                 := 32
WANT_NEW_API             := 1
NEED_DEINTERLACER        := 1
NEED_THREADING           := 1
NEED_TREMOR              := 1
GLES                     := 0
HAVE_OPENGL              := 0
HAVE_VULKAN              := 0
HAVE_CHD                 := 1
IS_X86                   := 0
FLAGS                    :=
HAVE_LIGHTREC            := 1
THREADED_RECOMPILER      := 1

ifeq ($(TARGET_ARCH),x86)
  IS_X86 := 1
endif

ifeq ($(HAVE_HW),1)
  # gles support will not compile
  #GLES        := 1
  #HAVE_OPENGL := 1

  ifneq ($(TARGET_ARCH_ABI),armeabi)
    HAVE_VULKAN := 1
    FLAGS       += -DHAVE_VULKAN
  endif
  FLAGS += -DHAVE_HW
endif

ifeq ($(HAVE_LIGHTREC),1)
  FLAGS += -DHAVE_ASHMEM
endif

include $(CORE_DIR)/Makefile.common

ifeq ($(HAVE_HW),1)
  INCFLAGS += -I$(CORE_DIR)/parallel-psx \
				  -I$(CORE_DIR)/parallel-psx/atlas \
				  -I$(CORE_DIR)/parallel-psx/vulkan \
				  -I$(CORE_DIR)/parallel-psx/renderer \
				  -I$(CORE_DIR)/parallel-psx/khronos/include \
				  -I$(CORE_DIR)/parallel-psx/glsl/prebuilt \
				  -I$(CORE_DIR)/parallel-psx/SPIRV-Cross \
				  -I$(CORE_DIR)/parallel-psx/vulkan/SPIRV-Cross \
				  -I$(CORE_DIR)/parallel-psx/vulkan/SPIRV-Cross/include \
				  -I$(CORE_DIR)/parallel-psx/util \
				  -I$(CORE_DIR)/parallel-psx/volk
endif

COREFLAGS := -funroll-loops $(INCFLAGS) -DMEDNAFEN_VERSION=\"0.9.26\" -DMEDNAFEN_VERSION_NUMERIC=926 -DPSS_STYLE=1 -D__LIBRETRO__ -D_LOW_ACCURACY_ -DINLINE="inline" $(FLAGS)
COREFLAGS += -DWANT_PSX_EMU $(GLFLAGS)

GIT_VERSION := " $(shell git rev-parse --short HEAD || echo unknown)"
ifneq ($(GIT_VERSION)," unknown")
  COREFLAGS += -DGIT_VERSION=\"$(GIT_VERSION)\"
endif

include $(CLEAR_VARS)
LOCAL_MODULE       := retro
LOCAL_SRC_FILES    := $(SOURCES_CXX) $(SOURCES_C)
LOCAL_CFLAGS       := $(COREFLAGS)
LOCAL_CXXFLAGS     := $(COREFLAGS) -std=c++11
LOCAL_LDFLAGS      := -Wl,-version-script=$(CORE_DIR)/link.T -ldl
LOCAL_LDLIBS       := -llog
LOCAL_CPP_FEATURES := exceptions
include $(BUILD_SHARED_LIBRARY)
