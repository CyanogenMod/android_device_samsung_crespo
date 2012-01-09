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

#include "SecCameraUtils.h"
#include <stdlib.h>

namespace android {

SecCameraArea::SecCameraArea(int left, int top, int right, int bottom, int weight) :
    m_left(left),
    m_top(top),
    m_right(right),
    m_bottom(bottom),
    m_weight(weight)
{
}

SecCameraArea::SecCameraArea(const char* str) {
    char* end;

    if (str != NULL && str[0] == '(') {
        m_left = (int)strtol(str+1, &end, 10);
        if (*end != ',') goto error;
        m_top = (int)strtol(end+1, &end, 10);
        if (*end != ',') goto error;
        m_right = (int)strtol(end+1, &end, 10);
        if (*end != ',') goto error;
        m_bottom = (int)strtol(end+1, &end, 10);
        if (*end != ',') goto error;
        m_weight = (int)strtol(end+1, &end, 10);
        if (*end != ')') goto error;
    }

    return;

error:
    m_left = m_top = m_right = m_bottom = m_weight = 0;
}

int SecCameraArea::getX(int width) {
    return (((m_left + m_right) / 2) + 1000) * width / 2000;
}

int SecCameraArea::getY(int height) {
    return (((m_top + m_bottom) / 2) + 1000) * height / 2000;
}

bool SecCameraArea::isDummy() {
    return m_left == 0 && m_top == 0 && m_right == 0 && m_bottom == 0;
}

String8 SecCameraArea::toString8() {
    return String8::format("(%d,%d,%d,%d,%d)",
        m_left, m_top, m_right, m_bottom, m_weight);
}

}