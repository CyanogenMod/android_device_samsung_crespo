# Copyright (C) 2009 The Android Open Source Project
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
# This file is the build configuration for a full Android
# build for dream hardware. This cleanly combines a set of
# device-specific aspects (drivers) with a device-agnostic
# product configuration (apps).
#

$(call inherit-product, build/target/product/generic.mk)


# Overrides
PRODUCT_MANUFACTURER := samsung
PRODUCT_NAME := crespo
PRODUCT_DEVICE := crespo
PRODUCT_MODEL := Andorid on Crespo
PRODUCT_LOCALES += en_US hdpi 

PRODUCT_PROPERTY_OVERRIDES += \
        keyguard.no_require_sim=true
