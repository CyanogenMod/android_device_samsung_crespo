LOCAL_PATH:= $(call my-dir)

ifneq ($(filter crespo crespo4g,$(TARGET_DEVICE)),)

include $(CLEAR_VARS)
LOCAL_SRC_FILES:= aplay.c alsa_pcm.c alsa_mixer.c
LOCAL_MODULE:= aplay
LOCAL_SHARED_LIBRARIES:= libc libcutils
LOCAL_MODULE_TAGS:= debug
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_SRC_FILES:= arec.c alsa_pcm.c
LOCAL_MODULE:= arec
LOCAL_SHARED_LIBRARIES:= libc libcutils
LOCAL_MODULE_TAGS:= debug
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_SRC_FILES:= amix.c alsa_mixer.c
LOCAL_MODULE:= amix
LOCAL_SHARED_LIBRARIES := libc libcutils
LOCAL_MODULE_TAGS:= debug
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_SRC_FILES:= AudioHardware.cpp alsa_mixer.c alsa_pcm.c
LOCAL_MODULE:= libaudio
LOCAL_STATIC_LIBRARIES:= libaudiointerface
LOCAL_SHARED_LIBRARIES:= libc libcutils libutils libmedia libhardware_legacy
ifeq ($(BOARD_HAVE_BLUETOOTH),true)
  LOCAL_SHARED_LIBRARIES += liba2dp
endif

ifeq ($(TARGET_SIMULATOR),true)
 LOCAL_LDLIBS += -ldl
else
 LOCAL_SHARED_LIBRARIES += libdl
endif

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_SRC_FILES:= AudioPolicyManager.cpp
LOCAL_MODULE:= libaudiopolicy
LOCAL_STATIC_LIBRARIES:= libaudiopolicybase
LOCAL_SHARED_LIBRARIES:= libc libcutils libutils libmedia
ifeq ($(BOARD_HAVE_BLUETOOTH),true)
  LOCAL_CFLAGS += -DWITH_A2DP
endif
include $(BUILD_SHARED_LIBRARY)

endif
