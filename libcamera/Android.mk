ifneq ($(filter crespo crespo4g,$(TARGET_DEVICE)),)

# When zero we link against libqcamera; when 1, we dlopen libqcamera.
ifeq ($(BOARD_CAMERA_LIBRARIES),libcamera)

DLOPEN_LIBSECCAMERA:=1

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_CFLAGS:=-fno-short-enums
LOCAL_CFLAGS+=-DDLOPEN_LIBSECCAMERA=$(DLOPEN_LIBSECCAMERA)

LOCAL_C_INCLUDES += $(LOCAL_PATH)/../include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../libs3cjpeg


LOCAL_SRC_FILES:= \
	SecCamera.cpp \
	SecCameraHWInterface.cpp


LOCAL_SHARED_LIBRARIES:= libutils libui liblog libbinder libcutils
LOCAL_SHARED_LIBRARIES+= libs3cjpeg
LOCAL_SHARED_LIBRARIES+= libcamera_client

ifeq ($(BOARD_USES_OVERLAY),true)
LOCAL_CFLAGS += -DBOARD_USES_OVERLAY
endif

ifeq ($(DLOPEN_LIBSECCAMERA),1)
LOCAL_SHARED_LIBRARIES+= libdl
endif

LOCAL_MODULE:= libcamera

LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

endif
endif
