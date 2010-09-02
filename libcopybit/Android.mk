# Copyright (C) 2008 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

ifeq ($(BOARD_USES_COPYBIT),true)

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_SHARED_LIBRARIES := liblog libutils

LOCAL_CFLAGS  += -DDEFAULT_FB_NUM=$(DEFAULT_FB_NUM) -DSLSI_S5PC110

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/../include \
	hardware/libhardware/modules/gralloc

LOCAL_SRC_FILES := copybit.cpp

LOCAL_MODULE := copybit.$(TARGET_BOARD_PLATFORM)
include $(BUILD_SHARED_LIBRARY)

endif
