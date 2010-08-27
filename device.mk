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
#

PRODUCT_COPY_FILES += \
	device/samsung/crespo/asound.conf:system/etc/asound.conf \
	device/samsung/crespo/vold.conf:system/etc/vold.conf \
	device/samsung/crespo/vold.fstab:system/etc/vold.fstab

PRODUCT_COPY_FILES += \
       device/samsung/crespo/egl.cfg:system/lib/egl/egl.cfg

# Get non-open-source aspects if available
$(call inherit-product-if-exists, vendor/samsung/crespo/device-vendor.mk)
