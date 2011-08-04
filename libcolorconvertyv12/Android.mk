ifneq ($(filter crespo crespo4g,$(TARGET_DEVICE)),)

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := \
	IYV12ColorConverter.c


LOCAL_MODULE := libyv12colorconvert

LOCAL_CFLAGS :=

LOCAL_ARM_MODE := arm

LOCAL_STATIC_LIBRARIES :=
LOCAL_SHARED_LIBRARIES :=

LOCAL_C_INCLUDES := \
	$(TOP)/frameworks/base/include/media/stagefright/openmax \
	$(TOP)/frameworks/media/libvideoeditor/include

include $(BUILD_SHARED_LIBRARY)
endif

