/*
** Copyright 2011, The Android Open-Source Project
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

//#define LOG_NDEBUG 0
#define LOG_TAG "ReSampler"

#include <utils/Log.h>
#include "ReSampler.h"

namespace android_audio_legacy {


//------------------------------------------------------------------------------
// speex based resampler
//------------------------------------------------------------------------------

#define RESAMPLER_QUALITY 2

ReSampler::ReSampler(uint32_t inSampleRate,
                                    uint32_t outSampleRate,
                                    uint32_t channelCount,
                                    BufferProvider* provider)
    :  mStatus(NO_INIT), mSpeexResampler(NULL), mProvider(provider),
       mInSampleRate(inSampleRate), mOutSampleRate(outSampleRate), mChannelCount(channelCount),
       mInBuf(NULL), mInBufSize(0)
{
    LOGV("ReSampler() cstor %p In SR %d Out SR %d channels %d",
         this, mInSampleRate, mOutSampleRate, mChannelCount);

    if (mProvider == NULL) {
        return;
    }

    int error;
    mSpeexResampler = speex_resampler_init(channelCount,
                                      inSampleRate,
                                      outSampleRate,
                                      RESAMPLER_QUALITY,
                                      &error);
    if (mSpeexResampler == NULL) {
        LOGW("ReSampler: Cannot create speex resampler: %s", speex_resampler_strerror(error));
        return;
    }

    reset();

    int frames = speex_resampler_get_input_latency(mSpeexResampler);
    mSpeexDelayNs = (int32_t)((1000000000 * (int64_t)frames) / mInSampleRate);
    frames = speex_resampler_get_output_latency(mSpeexResampler);
    mSpeexDelayNs += (int32_t)((1000000000 * (int64_t)frames) / mOutSampleRate);

    mStatus = NO_ERROR;
}

ReSampler::~ReSampler()
{
    free(mInBuf);

    if (mSpeexResampler != NULL) {
        speex_resampler_destroy(mSpeexResampler);
    }
}

void ReSampler::reset()
{
    mFramesIn = 0;
    mFramesRq = 0;

    if (mSpeexResampler != NULL) {
        speex_resampler_reset_mem(mSpeexResampler);
    }
}

int32_t ReSampler::delayNs()
{
    int32_t delay = (int32_t)((1000000000 * (int64_t)mFramesIn) / mInSampleRate);
    delay += mSpeexDelayNs;

    return delay;
}

// outputs a number of frames less or equal to *outFrameCount and updates *outFrameCount
// with the actual number of frames produced.
int ReSampler::resample(int16_t *out, size_t *outFrameCount)
{
    if (mStatus != NO_ERROR) {
        return mStatus;
    }

    if (out == NULL || outFrameCount == NULL) {
        return BAD_VALUE;
    }

    size_t framesRq = *outFrameCount;
    // update and cache the number of frames needed at the input sampling rate to produce
    // the number of frames requested at the output sampling rate
    if (framesRq != mFramesRq) {
        mFramesNeeded = (framesRq * mOutSampleRate) / mInSampleRate + 1;
        mFramesRq = framesRq;
    }

    size_t framesWr = 0;
    size_t inFrames = 0;
    while (framesWr < framesRq) {
        if (mFramesIn < mFramesNeeded) {
            // make sure that the number of frames present in mInBuf (mFramesIn) is at least
            // the number of frames needed to produce the number of frames requested at
            // the output sampling rate
            if (mInBufSize < mFramesNeeded) {
                mInBufSize = mFramesNeeded;
                mInBuf = (int16_t *)realloc(mInBuf, mInBufSize * mChannelCount * sizeof(int16_t));
            }
            BufferProvider::Buffer buf;
            buf.frameCount = mFramesNeeded - mFramesIn;
            mProvider->getNextBuffer(&buf);
            if (buf.raw == NULL) {
                break;
            }
            memcpy(mInBuf + mFramesIn * mChannelCount,
                    buf.raw,
                    buf.frameCount * mChannelCount * sizeof(int16_t));
            mFramesIn += buf.frameCount;
            mProvider->releaseBuffer(&buf);
        }

        size_t outFrames = framesRq - framesWr;
        inFrames = mFramesIn;
        if (mChannelCount == 1) {
            speex_resampler_process_int(mSpeexResampler,
                                        0,
                                        mInBuf,
                                        &inFrames,
                                        out + framesWr * mChannelCount,
                                        &outFrames);
        } else {
            speex_resampler_process_interleaved_int(mSpeexResampler,
                                        mInBuf,
                                        &inFrames,
                                        out + framesWr * mChannelCount,
                                        &outFrames);
        }
        framesWr += outFrames;
        mFramesIn -= inFrames;
        LOGW_IF((framesWr != framesRq) && (mFramesIn != 0),
                "ReSampler::resample() remaining %d frames in and %d frames out",
                mFramesIn, (framesRq - framesWr));
    }
    if (mFramesIn) {
        memmove(mInBuf,
                mInBuf + inFrames * mChannelCount,
                mFramesIn * mChannelCount * sizeof(int16_t));
    }
    *outFrameCount = framesWr;

    return NO_ERROR;
}

}; // namespace android_audio_legacy
