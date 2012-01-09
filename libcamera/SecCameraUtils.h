/*
**
** Copyright 2011, Havlena Petr <havlenapetr@gmail.com>
** Copyright 2011, The CyanogenMod Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#ifndef ANDROID_HARDWARE_CAMERA_SEC_UTILS_H
#define ANDROID_HARDWARE_CAMERA_SEC_UTILS_H

#include <utils/String8.h>

namespace android {

struct SecCameraArea {
    int m_left;
    int m_top;
    int m_right;
    int m_bottom;
    int m_weight;

    SecCameraArea(int left = 0, int top = 0, int right = 0, int bottom = 0, int weight = 0);
    SecCameraArea(const char* str);

    int getX(int width);
    int getY(int height);
    bool isDummy();
    String8 toString8();
};

}; // namespace android

#endif // ANDROID_HARDWARE_CAMERA_SEC_UTILS_H