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

import common

def FullOTA_InstallEnd(info):
  try:
    bootloader_img = info.input_zip.read("RADIO/bootloader.img")
    common.ZipWriteStr(info.output_zip, "bootloader.img", bootloader_img)
    info.script.WriteRawImage("bootloader", "bootloader.img")
  except KeyError:
    print "no bootloader.img in target_files; skipping install"

  try:
    radio_img = info.input_zip.read("RADIO/radio.img")
    common.ZipWriteStr(info.output_zip, "radio.img", radio_img)
    info.script.WriteRawImage("radio", "radio.img")
  except KeyError:
    print "no radio.img in target_files; skipping install"

def IncrementalOTA_InstallEnd(info):
  try:
    target_bootloader_img = info.target_zip.read("RADIO/bootloader.img")
    try:
      source_bootloader_img = info.source_zip.read("RADIO/bootloader.img")
    except KeyError:
      source_bootloader_img = None

    if source_bootloader_img == target_bootloader_img:
      print "bootloader unchanged; skipping"
    else:
      common.ZipWriteStr(info.output_zip, "bootloader.img", bootloader_img)
      info.script.WriteRawImage("bootloader", "bootloader.img")

  except KeyError:
    print "no bootloader.img in target target_files; skipping install"

  try:
    target_radio_img = info.target_zip.read("RADIO/radio.img")
    try:
      source_radio_img = info.source_zip.read("RADIO/radio.img")
    except KeyError:
      source_radio_img = None

    if source_radio_img == target_radio_img:
      print "radio unchanged; skipping"
    else:
      # TODO: send radio image as binary patch

      common.ZipWriteStr(info.output_zip, "radio.img", radio_img)
      info.script.WriteRawImage("radio", "radio.img")
  except KeyError:
    print "no radio.img in target target_files; skipping install"
