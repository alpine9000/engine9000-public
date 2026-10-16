APP_PLATFORM := android-18
LOCAL_PATH := $(call my-dir)

ROOT_DIR := $(LOCAL_PATH)/../..
CORE_DIR := $(ROOT_DIR)

include $(ROOT_DIR)/libretro/Makefile.common

COREFLAGS := -DANDROID -D__LIBRETRO__ -DZ7_ST $(INCFLAGS) $(FLAGS)

GIT_VERSION := " $(shell git rev-parse --short HEAD || echo unknown)"
ifneq ($(GIT_VERSION)," unknown")
  COREFLAGS += -DGIT_VERSION=\"$(GIT_VERSION)\"
endif

include $(CLEAR_VARS)
LOCAL_MODULE    := retro
LOCAL_SRC_FILES := $(SOURCES_C)
LOCAL_CFLAGS    := $(COREFLAGS)
LOCAL_LDFLAGS   := -Wl,-version-script=$(ROOT_DIR)/libretro/link.T
LOCAL_LDLIBS    := -lz
include $(BUILD_SHARED_LIBRARY)
