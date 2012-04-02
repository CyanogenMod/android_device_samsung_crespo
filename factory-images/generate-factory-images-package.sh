#!/bin/sh

# Copyright (C) 2011 The Android Open Source Project
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

# 189904 = GRK39F
# 299849 = IMM76D

PRODUCT=soju
DEVICE=crespo
BUILD=299849
VERSION=imm76d
RADIOSRC=radio.xx.img
BOOTLOADER=i9020xxkl1
RADIO=i9020xxki1

source ../../../common/generate-factory-images-common.sh

PRODUCT=sojua
DEVICE=crespo
BUILD=299849
VERSION=imm76d
RADIOSRC=radio.uc.img
BOOTLOADER=i9020xxkl1
RADIO=i9020uckj1

source ../../../common/generate-factory-images-common.sh

PRODUCT=sojuk
DEVICE=crespo
BUILD=189904
VERSION=grk39f
RADIOSRC=radio.kr.img
BOOTLOADER=i9020xxka3
RADIO=m200krkc1

source ../../../common/generate-factory-images-common.sh
