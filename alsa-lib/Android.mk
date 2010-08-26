# external/alsa-lib/Android.mk
#
# Copyright 2008 Wind River Systems
#

ifeq ($(strip $(BOARD_USES_ALSA_AUDIO)),true)

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

##
## Copy ALSA configuration files to rootfs
##
TARGET_ALSA_CONF_DIR := $(TARGET_OUT)/usr/share/alsa
LOCAL_ALSA_CONF_DIR  := $(LOCAL_PATH)/src/conf

copy_from := \
	alsa.conf \
	pcm/dsnoop.conf \
	pcm/modem.conf \
	pcm/dpl.conf \
	pcm/default.conf \
	pcm/surround51.conf \
	pcm/surround41.conf \
	pcm/surround50.conf \
	pcm/dmix.conf \
	pcm/center_lfe.conf \
	pcm/surround40.conf \
	pcm/side.conf \
	pcm/iec958.conf \
	pcm/rear.conf \
	pcm/surround71.conf \
	pcm/front.conf \
	cards/aliases.conf

copy_to   := $(addprefix $(TARGET_ALSA_CONF_DIR)/,$(copy_from))
copy_from := $(addprefix $(LOCAL_ALSA_CONF_DIR)/,$(copy_from))

$(copy_to) : $(TARGET_ALSA_CONF_DIR)/% : $(LOCAL_ALSA_CONF_DIR)/% | $(ACP)
	$(transform-prebuilt-to-target)

ALL_PREBUILT += $(copy_to)

include $(CLEAR_VARS)

LOCAL_MODULE := libasound

LOCAL_ARM_MODE := arm

LOCAL_C_INCLUDES += $(LOCAL_PATH)/include

# libasound must be compiled with -fno-short-enums, as it makes extensive
# use of enums which are often type casted to unsigned ints.
LOCAL_CFLAGS := \
	-fPIC -DPIC -D_POSIX_SOURCE \
	-DALSA_CONFIG_DIR=\"/system/usr/share/alsa\" \
	-DALSA_PLUGIN_DIR=\"/system/usr/lib/alsa-lib\" \
	-DALSA_DEVICE_DIRECTORY=\"/dev/snd/\"

LOCAL_SRC_FILES := $(sort $(call all-c-files-under, src))

# It is easier to exclude the ones we don't want...
#
LOCAL_SRC_FILES := $(filter-out src/alisp/alisp_snd.c, $(LOCAL_SRC_FILES))
LOCAL_SRC_FILES := $(filter-out src/compat/hsearch_r.c, $(LOCAL_SRC_FILES))
LOCAL_SRC_FILES := $(filter-out src/control/control_shm.c, $(LOCAL_SRC_FILES))
LOCAL_SRC_FILES := $(filter-out src/pcm/pcm_d%.c, $(LOCAL_SRC_FILES))
LOCAL_SRC_FILES := $(filter-out src/pcm/pcm_ladspa.c, $(LOCAL_SRC_FILES))
LOCAL_SRC_FILES := $(filter-out src/pcm/pcm_shm.c, $(LOCAL_SRC_FILES))
LOCAL_SRC_FILES := $(filter-out src/pcm/scopes/level.c, $(LOCAL_SRC_FILES))
LOCAL_SRC_FILES := $(filter-out src/shmarea.c, $(LOCAL_SRC_FILES))

include $(BUILD_STATIC_LIBRARY)

endif

