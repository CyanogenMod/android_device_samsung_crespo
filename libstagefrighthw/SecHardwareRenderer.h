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

#ifndef SEC_HARDWARE_RENDERER_H_

#define SEC_HARDWARE_RENDERER_H_

#include <media/stagefright/VideoRenderer.h>
#include <utils/RefBase.h>
#include <utils/Vector.h>

#include <OMX_Component.h>

#include <binder/MemoryHeapBase.h>
#include <binder/MemoryHeapPmem.h>

namespace android {

class ISurface;
class Overlay;

class SecHardwareRenderer : public VideoRenderer {
public:
    SecHardwareRenderer(
            const sp<ISurface> &surface,
            size_t displayWidth, size_t displayHeight,
            size_t decodedWidth, size_t decodedHeight,
            OMX_COLOR_FORMATTYPE colorFormat,
            int32_t rotationDegrees,
            bool fromHardwareDecoder);

    virtual ~SecHardwareRenderer();

    status_t initCheck() const { return mInitCheck; }

    virtual void render(
            const void *data, size_t size, void *platformPrivate);


private:
    sp<ISurface> mISurface;
    size_t mDisplayWidth, mDisplayHeight;
    size_t mDecodedWidth, mDecodedHeight;
    OMX_COLOR_FORMATTYPE mColorFormat;
    status_t mInitCheck;
    size_t mFrameSize;
    sp<Overlay> mOverlay;
    sp<MemoryHeapBase> mMemoryHeap;
    Vector<void *> mOverlayAddresses;
    bool mIsFirstFrame;
    int mNumBuf;
    size_t mIndex;
    bool mCustomFormat;


    SecHardwareRenderer(const SecHardwareRenderer &);
    SecHardwareRenderer &operator=(const SecHardwareRenderer &);

    void handleYUV420Planar(const void *, size_t);
};

}  // namespace android

#endif  // SEC_HARDWARE_RENDERER_H_

