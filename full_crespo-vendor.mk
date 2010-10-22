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

# This file is an "interesting" abuse of the build system.
#
# So, here's the reason why this file exists:
#
# The live wallpaper stuff is all open-source, but it fundamentally
# relies on non-open-source features (specifically, OpenGL drivers,
# which don't seem to ever get open-sourced). Because of that, we can't
# use them in pure open-source builds (they crash).
#
# The clean solution would be to maintain two entirely separate
# device and product configurations, one purely open-source and the
# other that'd use proprietary drivers.
#
# That's very nice in theory, but the reality is that in such a setup
# the two variants would inevitably drift. Been there, done that.
#
# So, we're in a situation where we want to include those open-source
# packages only in non-open-source builds. To be as transparent as
# possible, we want to do that in an open-source way.
#
# One approach (which has been used in the past) is to pretend that
# those files are drivers, so that when we include the driver-level
# modules that are necessary we also include those apps. That works
# fine if you only ever build a single configuration in open-source
# environments with proprietary drivers added. However it breaks down
# once you try to do multiple builds: those are very high-level features
# that shouldn't clutter the build of someone trying to do a minimally
# booting system.
#
# The approach I'm using here really considers these as product-level,
# not device-level. The plain product definition doesn't include this
# file directly. Instead, it conditionally includes a file on the
# proprietary side, which trampolines back to this file. In pure
# open-source builds, this file is never reached. In private builds
# with the true vendor tree, the trampoline exists and bounces back
# here. In open-source builds with proprietary drivers added, a
# trampoline is created along with the proprietary drivers.
#
# That's a long explanation, but, really, it just works. Trust me.

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
DEVICE_PACKAGE_OVERLAYS := vendor/samsung/crespo/overlay
