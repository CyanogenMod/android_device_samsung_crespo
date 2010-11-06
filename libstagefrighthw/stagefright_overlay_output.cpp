/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "SecHardwareRenderer.h"

#include <media/stagefright/HardwareAPI.h>

using android::sp;
using android::ISurface;
using android::VideoRenderer;

VideoRenderer *createRendererWithRotation(
        const sp<ISurface> &surface,
        const char *componentName,
        OMX_COLOR_FORMATTYPE colorFormat,
        size_t displayWidth, size_t displayHeight,
        size_t decodedWidth, size_t decodedHeight,
        int32_t rotationDegrees) {
    using android::SecHardwareRenderer;

    bool fromHardwareDecoder = !strncmp(componentName, "OMX.SEC.", 8);

    SecHardwareRenderer *renderer =
        new SecHardwareRenderer(
                surface, displayWidth, displayHeight,
                decodedWidth, decodedHeight,
                colorFormat,
                rotationDegrees,
                fromHardwareDecoder);

    if (renderer->initCheck() != android::OK) {
        delete renderer;
        renderer = NULL;
    }

    return renderer;
}

