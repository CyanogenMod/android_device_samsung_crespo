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

# 299849 = IMM76D

PRODUCT=soju
DEVICE=crespo
BUILD=299849
VERSION=imm76d
RADIOSRC=radio.xx.img
BOOTLOADER=i9020xxkl1
RADIO=i9020xxki1

if test "$RADIOSRC" = ""
then
  RADIOSRC=radio.img
fi
rm -rf tmp
mkdir -p tmp/$PRODUCT-$VERSION
unzip -d tmp signed-$PRODUCT-target_files-$BUILD.zip RADIO/$RADIOSRC RADIO/bootloader.img
if test "$CDMARADIO" != ""
then
  unzip -d tmp signed-$PRODUCT-target_files-$BUILD.zip RADIO/radio-cdma.img
fi
cp signed-$PRODUCT-img-$BUILD.zip tmp/$PRODUCT-$VERSION/image-$PRODUCT-$VERSION.zip
cp tmp/RADIO/bootloader.img tmp/$PRODUCT-$VERSION/bootloader-$DEVICE-$BOOTLOADER.img
cp tmp/RADIO/$RADIOSRC tmp/$PRODUCT-$VERSION/radio-$DEVICE-$RADIO.img
if test "$CDMARADIO" != ""
then
  cp tmp/RADIO/radio-cdma.img tmp/$PRODUCT-$VERSION/radio-cdma-$DEVICE-$CDMARADIO.img
fi
cat > tmp/$PRODUCT-$VERSION/flash-all.sh << EOF
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

fastboot flash bootloader bootloader-$DEVICE-$BOOTLOADER.img
fastboot reboot-bootloader
sleep 5
fastboot flash radio radio-$DEVICE-$RADIO.img
fastboot reboot-bootloader
sleep 5
EOF
if test "$CDMARADIO" != ""
then
cat >> tmp/$PRODUCT-$VERSION/flash-all.sh << EOF
fastboot flash radio-cdma radio-cdma-$DEVICE-$CDMARADIO.img
fastboot reboot-bootloader
sleep 5
EOF
fi
cat >> tmp/$PRODUCT-$VERSION/flash-all.sh << EOF
fastboot -w update image-$PRODUCT-$VERSION.zip
EOF
chmod a+x tmp/$PRODUCT-$VERSION/flash-all.sh
cat > tmp/$PRODUCT-$VERSION/flash-base.sh << EOF
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

fastboot flash bootloader bootloader-$DEVICE-$BOOTLOADER.img
fastboot reboot-bootloader
sleep 5
fastboot flash radio radio-$DEVICE-$RADIO.img
fastboot reboot-bootloader
sleep 5
EOF
if test "$CDMARADIO" != ""
then
cat >> tmp/$PRODUCT-$VERSION/flash-base.sh << EOF
fastboot flash radio-cdma radio-cdma-$DEVICE-$CDMARADIO.img
fastboot reboot-bootloader
sleep 5
EOF
fi
chmod a+x tmp/$PRODUCT-$VERSION/flash-base.sh
(cd tmp ; tar zcvf ../$PRODUCT-$VERSION-factory.tgz $PRODUCT-$VERSION)
mv $PRODUCT-$VERSION-factory.tgz $PRODUCT-$VERSION-factory-$(sha1sum < $PRODUCT-$VERSION-factory.tgz | cut -b -8).tgz
rm -rf tmp
