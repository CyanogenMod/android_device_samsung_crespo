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


# DO NOT USE OR MODIFY
# This is a deprecated product configuration, which is kept for
# legacy purposes. It is identical to full_crespo. You should
# be using full_crespo instead.

$(call inherit-product, device/samsung/crespo/full_crespo.mk)

ifeq ($(TARGET_PRODUCT),crespo)
  $(warning ************************************)
  $(warning * The crespo config is deprecated. *)
  $(warning * Please use full_crespo instead.  *)
  $(warning ************************************)
endif

# Overrides
PRODUCT_NAME := crespo
PRODUCT_MODEL := Deprecated crespo config
