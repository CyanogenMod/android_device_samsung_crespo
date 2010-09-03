# hardware/libaudio-alsa/Android.mk
#
# Copyright 2008 Wind River Systems
#

ifeq ($(TARGET_DEVICE),crespo)
ifeq ($(filter-out s5pc110 s5pc100 s5p6440,$(TARGET_BOARD_PLATFORM)),)
ifeq ($(BOARD_USES_GENERIC_AUDIO),false)

  LOCAL_PATH := $(call my-dir)

  include $(CLEAR_VARS)

  LOCAL_ARM_MODE := arm
  LOCAL_CFLAGS := -D_POSIX_SOURCE
  LOCAL_WHOLE_STATIC_LIBRARIES := libasound

  ifneq ($(ALSA_DEFAULT_SAMPLE_RATE),)
    LOCAL_CFLAGS += -DALSA_DEFAULT_SAMPLE_RATE=$(ALSA_DEFAULT_SAMPLE_RATE)
  endif

# Samsung Feature
ifeq ($(TARGET_BOARD_PLATFORM),s5pc110)
  LOCAL_CFLAGS += -DSLSI_S5PC110

# Samsung Driver Feature
#   LOCAL_CFLAGS += -DSEC_SWP_SOUND -DSEC_IPC -DPOWER_GATING -DSYNCHRONIZE_CP -DBT_NR_EC_ONOFF
   LOCAL_CFLAGS += -DSEC_SWP_SOUND -DSEC_IPC 
   LOCAL_CFLAGS += -DTURN_ON_DEVICE_ONLY_USE  
endif

ifeq ($(TARGET_BOARD_PLATFORM),s5pc100)
  LOCAL_CFLAGS += -DSLSI_S5PC100
endif

  LOCAL_C_INCLUDES += device/samsung/crespo/alsa-lib/include
  LOCAL_SRC_FILES := AudioHardwareALSA.cpp

  LOCAL_MODULE := libaudio

  LOCAL_STATIC_LIBRARIES += libaudiointerface

  LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libutils \
    libmedia \
    libhardware_legacy \
    libdl \
    libc

ifeq ($(BOARD_HAVE_BLUETOOTH),true)
  LOCAL_SHARED_LIBRARIES += liba2dp
endif

ifneq ($(NO_IPC_ALSA_RILD),true)
  LOCAL_SHARED_LIBRARIES += libsecril-client
  LOCAL_CFLAGS  +=  -DIPC_ALSA_RILD
endif
  include $(BUILD_SHARED_LIBRARY)

# To build audiopolicy library 
include $(CLEAR_VARS)

LOCAL_SRC_FILES:=               \
    AudioPolicyManager.cpp

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libutils \
    libmedia

LOCAL_MODULE:= libaudiopolicy

ifeq ($(BOARD_HAVE_BLUETOOTH),true)
  LOCAL_CFLAGS += -DWITH_A2DP
endif

include $(BUILD_SHARED_LIBRARY)

endif
endif
endif
