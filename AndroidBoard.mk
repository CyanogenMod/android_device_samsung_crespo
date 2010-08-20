# Copyright (C) 2007 The Android Open Source Project
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

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
target_init_rc_file := $(TARGET_ROOT_OUT)/init.rc
$(target_init_rc_file) : $(LOCAL_PATH)/init.rc | $(ACP)
	$(transform-prebuilt-to-target)
ALL_PREBUILT += $(target_init_rc_file)

target_hw_init_rc_file := $(TARGET_ROOT_OUT)/init.smdkc110.rc
$(target_hw_init_rc_file) : $(LOCAL_PATH)/init.smdkc110.rc | $(ACP)
	$(transform-prebuilt-to-target)
ALL_PREBUILT += $(target_hw_init_rc_file)

$(INSTALLED_RAMDISK_TARGET): $(target_init_rc_file) $(target_hw_init_rc_file) 

# to build the bootloader you need the common boot stuff,
# the architecture specific stuff, and the board specific stuff
# include bootloader/legacy/Android.mk

# Use the non-open-source parts, if they're present
-include vendor/samsung/crespo/AndroidBoardVendor.mk
