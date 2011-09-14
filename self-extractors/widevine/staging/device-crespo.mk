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

LOCAL_PATH := vendor/widevine/crespo

# Widevine blobs necessary for Nexus S hardware
PRODUCT_COPY_FILES := \
    $(LOCAL_PATH)/proprietary/com.google.widevine.software.drm.xml:system/etc/permissions/com.google.widevine.software.drm.xml \
    $(LOCAL_PATH)/proprietary/libdrmwvmplugin.so:system/lib/drm/libdrmwvmplugin.so \
    $(LOCAL_PATH)/proprietary/libwvdrm_L3.so:system/lib/libwvdrm_L3.so \
    $(LOCAL_PATH)/proprietary/libwvm.so:system/lib/libwvm.so \
    $(LOCAL_PATH)/proprietary/libWVStreamControlAPI_L3.so:system/lib/libWVStreamControlAPI_L3.so \

