# Copyright (C) 2010 The Android Open Source Project
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

# Live wallpaper packages
PRODUCT_PACKAGES := \
    LiveWallpapers \
    LiveWallpapersPicker \
    MagicSmokeWallpapers \
    VisualizationWallpapers \
    librs_jni

# Publish that we support the live wallpaper feature.
PRODUCT_COPY_FILES := \
    packages/wallpapers/LivePicker/android.software.live_wallpaper.xml:/system/etc/permissions/android.software.live_wallpaper.xml

# Pick up overlay for features that depend on non-open-source files
DEVICE_PACKAGE_OVERLAYS := vendor/imgtec/crespo/overlay

# Imgtec blobs necessary for crespo
PRODUCT_COPY_FILES += \
    vendor/imgtec/crespo/proprietary/pvrsrvinit:system/vendor/bin/pvrsrvinit \
    vendor/imgtec/crespo/proprietary/libEGL_POWERVR_SGX540_120.so:system/vendor/lib/egl/libEGL_POWERVR_SGX540_120.so \
    vendor/imgtec/crespo/proprietary/libGLESv1_CM_POWERVR_SGX540_120.so:system/vendor/lib/egl/libGLESv1_CM_POWERVR_SGX540_120.so \
    vendor/imgtec/crespo/proprietary/libGLESv2_POWERVR_SGX540_120.so:system/vendor/lib/egl/libGLESv2_POWERVR_SGX540_120.so \
    vendor/imgtec/crespo/proprietary/gralloc.s5pc110.so:system/vendor/lib/hw/gralloc.s5pc110.so \
    vendor/imgtec/crespo/proprietary/libglslcompiler.so:system/vendor/lib/libglslcompiler.so \
    vendor/imgtec/crespo/proprietary/libIMGegl.so:system/vendor/lib/libIMGegl.so \
    vendor/imgtec/crespo/proprietary/libpvr2d.so:system/vendor/lib/libpvr2d.so \
    vendor/imgtec/crespo/proprietary/libpvrANDROID_WSEGL.so:system/vendor/lib/libpvrANDROID_WSEGL.so \
    vendor/imgtec/crespo/proprietary/libPVRScopeServices.so:system/vendor/lib/libPVRScopeServices.so \
    vendor/imgtec/crespo/proprietary/libsrv_init.so:system/vendor/lib/libsrv_init.so \
    vendor/imgtec/crespo/proprietary/libsrv_um.so:system/vendor/lib/libsrv_um.so \
    vendor/imgtec/crespo/proprietary/libusc.so:system/vendor/lib/libusc.so
