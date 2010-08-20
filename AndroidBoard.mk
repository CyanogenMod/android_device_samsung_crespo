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

PRODUCT_COPY_FILES += \
	device/samsung/crespo/asound.conf:system/etc/asound.conf \
	device/samsung/crespo/vold.conf:system/etc/vold.conf \
	device/samsung/crespo/vold.fstab:system/etc/vold.fstab

ifeq ($(BOARD_USES_HGL),true)
PRODUCT_COPY_FILES += \
       device/samsung/crespo/egl.cfg:system/lib/egl/egl.cfg \
       vendor/samsung/crespo/pvrsrvkm.ko:root/modules/pvrsrvkm.ko \
       vendor/samsung/crespo/s3c_lcd.ko:root/modules/s3c_lcd.ko \
       vendor/samsung/crespo/s3c_bc.ko:root/modules/s3c_bc.ko \
       vendor/samsung/crespo/gralloc.s5pc110.so:system/lib/hw/gralloc.s5pc110.so \
       vendor/samsung/crespo/libEGL_POWERVR_SGX540_120.so:system/lib/egl/libEGL_POWERVR_SGX540_120.so \
       vendor/samsung/crespo/libGLESv1_CM_POWERVR_SGX540_120.so:system/lib/egl/libGLESv1_CM_POWERVR_SGX540_120.so \
       vendor/samsung/crespo/libGLESv2_POWERVR_SGX540_120.so:system/lib/egl/libGLESv2_POWERVR_SGX540_120.so \
       vendor/samsung/crespo/libsrv_um.so:system/lib/libsrv_um.so \
       vendor/samsung/crespo/libusc.so:system/lib/libusc.so \
       vendor/samsung/crespo/libsrv_init.so:system/lib/libsrv_init.so \
       vendor/samsung/crespo/libIMGegl.so:system/lib/libIMGegl.so \
       vendor/samsung/crespo/libpvr2d.so:system/lib/libpvr2d.so \
       vendor/samsung/crespo/libPVRScopeServices.so:system/lib/libPVRScopeServices.so \
       vendor/samsung/crespo/libglslcompiler.so:system/lib/libglslcompiler.so \
       vendor/samsung/crespo/libpvrANDROID_WSEGL.so:system/lib/libpvrANDROID_WSEGL.so \
       vendor/samsung/crespo/pvrsrvinit:system/bin/pvrsrvinit
endif

# to build the bootloader you need the common boot stuff,
# the architecture specific stuff, and the board specific stuff
# include bootloader/legacy/Android.mk

# Use the non-open-source parts, if they're present
-include vendor/samsung/crespo/AndroidBoardVendor.mk
