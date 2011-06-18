/*
** Copyright 2008, The Android Open-Source Project
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

#ifndef ANDROID_RESAMPLER_H
#define ANDROID_RESAMPLER_H

#include <stdint.h>
#include <sys/types.h>
#include <hardware_legacy/AudioSystemLegacy.h>
#include "speex/speex_resampler.h"

namespace android_audio_legacy {

class ReSampler {
public:

    class BufferProvider
    {
    public:

        struct Buffer {
            union {
                void*       raw;
                short*      i16;
                int8_t*     i8;
            };
            size_t frameCount;
        };

        virtual ~BufferProvider() {}

        virtual status_t getNextBuffer(Buffer* buffer) = 0;
        virtual void releaseBuffer(Buffer* buffer) = 0;
    };

    ReSampler(uint32_t inSampleRate,
              uint32_t outSampleRate,
              uint32_t channelCount,
              BufferProvider* provider);

    virtual ~ReSampler();

            status_t initCheck() { return mStatus; }
            void reset();
            int resample(int16_t* out, size_t *outFrameCount);
            int32_t delayNs();


private:
    status_t    mStatus;                    // init status
    SpeexResamplerState *mSpeexResampler;   // handle on speex resampler
    BufferProvider* mProvider;              // buffer provider installed by client
    uint32_t mInSampleRate;                 // input sampling rate
    uint32_t mOutSampleRate;                // output sampling rate
    uint32_t mChannelCount;                 // number of channels
    int16_t *mInBuf;                        // input buffer
    size_t mInBufSize;                      // input buffer size
    size_t mFramesIn;                       // number of frames in input buffer
    size_t mFramesRq;                       // cached number of output frames
    size_t mFramesNeeded;                   // minimum number of input frames to produce mFramesRq
                                            // output frames
    int32_t mSpeexDelayNs;                  // delay introduced by speex resampler in ns
};

}; // namespace android_audio_legacy

#endif // ANDROID_RESAMPLER_H
