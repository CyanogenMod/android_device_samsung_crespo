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

#define LOG_TAG "SecHardwareRenderer"
#define LOG_NDEBUG 0
#include <utils/Log.h>

#include "SecHardwareRenderer.h"

#include <media/stagefright/MediaDebug.h>
#include <surfaceflinger/ISurface.h>
#include <ui/Overlay.h>

#include <hardware/hardware.h>

#include "v4l2_utils.h"
#include "utils/Timers.h"

#define CACHEABLE_BUFFERS 0x1

#define USE_ZERO_COPY
//#define SEC_DEBUG

namespace android {

////////////////////////////////////////////////////////////////////////////////

SecHardwareRenderer::SecHardwareRenderer(
        const sp<ISurface> &surface,
        size_t displayWidth, size_t displayHeight,
        size_t decodedWidth, size_t decodedHeight,
        OMX_COLOR_FORMATTYPE colorFormat,
        int32_t rotationDegrees,
        bool fromHardwareDecoder)
    : mISurface(surface),
      mDisplayWidth(displayWidth),
      mDisplayHeight(displayHeight),
      mDecodedWidth(decodedWidth),
      mDecodedHeight(decodedHeight),
      mColorFormat(colorFormat),
      mInitCheck(NO_INIT),
      mFrameSize(mDecodedWidth * mDecodedHeight * 2),
      mIsFirstFrame(true),
      mCustomFormat(false),
      mIndex(0) {

    CHECK(mISurface.get() != NULL);
    CHECK(mDecodedWidth > 0);
    CHECK(mDecodedHeight > 0);

    if (colorFormat != OMX_COLOR_FormatCbYCrY
            && colorFormat != OMX_COLOR_FormatYUV420Planar
            && colorFormat != OMX_COLOR_FormatYUV420SemiPlanar) {
        LOGE("Invalid colorFormat (0x%x)", colorFormat);
        return;
    }

    uint32_t orientation;
    switch (rotationDegrees) {
        case 0: orientation = ISurface::BufferHeap::ROT_0; break;
        case 90: orientation = ISurface::BufferHeap::ROT_90; break;
        case 180: orientation = ISurface::BufferHeap::ROT_180; break;
        case 270: orientation = ISurface::BufferHeap::ROT_270; break;
        default: orientation = ISurface::BufferHeap::ROT_0; break;
    }

    sp<OverlayRef> ref;

#if defined (USE_ZERO_COPY)
    if (fromHardwareDecoder) {
        ref = mISurface->createOverlay(
                mDecodedWidth, mDecodedHeight,
                HAL_PIXEL_FORMAT_CUSTOM_YCbCr_420_SP, orientation);
        mCustomFormat = true;
    }
#else
    else
    {
        ref = mISurface->createOverlay(
                mDecodedWidth, mDecodedHeight, HAL_PIXEL_FORMAT_YCbCr_420_P,
                orientation);
    }
#endif

    if (ref.get() == NULL) {
        LOGE("Unable to create the overlay!");
        return;
    }

    mOverlay = new Overlay(ref);
    mOverlay->setParameter(CACHEABLE_BUFFERS, 0);

    mNumBuf = mOverlay->getBufferCount();

    if (mCustomFormat) {
        mFrameSize = 32;
        mMemoryHeap = new MemoryHeapBase(mNumBuf * mFrameSize);
    } else {
        for (size_t i = 0; i < (size_t)mNumBuf; ++i) {
            void *addr = mOverlay->getBufferAddress((void *)i);
            mOverlayAddresses.push(addr);
        }
    }

    mInitCheck = OK;
}

SecHardwareRenderer::~SecHardwareRenderer() {

    if(mMemoryHeap != NULL)
        mMemoryHeap.clear();

    if (mOverlay.get() != NULL) {
        mOverlay->destroy();
        mOverlay.clear();
    }
}

void SecHardwareRenderer::handleYUV420Planar(
        const void *data, size_t size) {

    int FrameSize;
    uint8_t* pPhyYAddr;
    uint8_t* pPhyCAddr;
    int AddrSize;
    size_t offset;

    CHECK(size >= (mDecodedWidth * mDecodedHeight * 3) / 2);

    offset = mIndex * mFrameSize;
    void *dst = (uint8_t *)mMemoryHeap->getBase() + offset;

    AddrSize = sizeof(void *);
    memcpy(&FrameSize, data, sizeof(FrameSize));
    memcpy(&pPhyYAddr, data + sizeof(FrameSize), sizeof(pPhyYAddr));
    memcpy(&pPhyCAddr, data + sizeof(FrameSize) + (AddrSize * 1), sizeof(pPhyCAddr));

    memcpy(dst , &pPhyYAddr, sizeof(pPhyYAddr));
    memcpy(dst  + sizeof(pPhyYAddr) , &pPhyCAddr, sizeof(pPhyCAddr));
    memcpy(dst  + sizeof(pPhyYAddr) + sizeof(pPhyCAddr), &mIndex, sizeof(mIndex));
}

void SecHardwareRenderer::render(
        const void *data, size_t size, void *platformPrivate) {

    if (mOverlay.get() == NULL) {
        return;
    }

    if (mCustomFormat) {
        /* zero copy solution case */

        overlay_buffer_t dst = (uint8_t *)mMemoryHeap->getBase() + mIndex*mFrameSize;

        if (mColorFormat == OMX_COLOR_FormatYUV420Planar ||
            mColorFormat == OMX_COLOR_FormatYUV420SemiPlanar) {
            handleYUV420Planar(data, size);
        }

        if (mOverlay->queueBuffer(dst) == ALL_BUFFERS_FLUSHED) {
           mIsFirstFrame = true;
           if (mOverlay->queueBuffer((void *)dst) != 0) {
                return;
           }
        }

        if (++mIndex == mNumBuf) {
            mIndex = 0;
        }

        overlay_buffer_t overlay_buffer;
        if (!mIsFirstFrame) {
            status_t err = mOverlay->dequeueBuffer(&overlay_buffer);
            if (err == ALL_BUFFERS_FLUSHED) {
                mIsFirstFrame = true;
            } else {
                return;
            }
        } else {
            mIsFirstFrame = false;
        }
    } else {
        /* normal frame case */
        if (mColorFormat == OMX_COLOR_FormatYUV420Planar) {
            memcpy(mOverlayAddresses[mIndex], data, size);
        }

        if (mOverlay->queueBuffer((void *)mIndex) == ALL_BUFFERS_FLUSHED) {
            mIsFirstFrame = true;
            if (mOverlay->queueBuffer((void *)mIndex) != 0) {
                return;
            }
        }

        if (++mIndex == mNumBuf) {
            mIndex = 0;
        }

        overlay_buffer_t overlay_buffer;
        if (!mIsFirstFrame) {
            status_t err = mOverlay->dequeueBuffer(&overlay_buffer);

            if (err == ALL_BUFFERS_FLUSHED) {
                mIsFirstFrame = true;
            } else {
                return;
            }
        } else {
            mIsFirstFrame = false;
        }
    }
}

}  // namespace android

