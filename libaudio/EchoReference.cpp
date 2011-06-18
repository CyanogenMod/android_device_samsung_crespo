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
#define LOG_TAG "EchoReference"

#include <utils/Log.h>
#include "EchoReference.h"

namespace android_audio_legacy {

//------------------------------------------------------------------------------
// Echo reference buffer
//------------------------------------------------------------------------------

EchoReference::EchoReference(audio_format_t rdFormat,
                                            uint32_t rdChannelCount,
                                            uint32_t rdSamplingRate,
                                            audio_format_t wrFormat,
                                            uint32_t wrChannelCount,
                                            uint32_t wrSamplingRate)
: mStatus (NO_INIT), mState(ECHOREF_IDLE),
  mRdFormat(rdFormat), mRdChannelCount(rdChannelCount), mRdSamplingRate(rdSamplingRate),
  mWrFormat(wrFormat), mWrChannelCount(wrChannelCount), mWrSamplingRate(wrSamplingRate),
  mBuffer(NULL), mBufSize(0), mFramesIn(0), mWrBuf(NULL), mWrBufSize(0), mWrFramesIn(0),
  mDownSampler(NULL)
{
    LOGV("EchoReference cstor");
    if (rdFormat != AUDIO_FORMAT_PCM_16_BIT ||
            rdFormat != wrFormat) {
        LOGW("EchoReference cstor bad format rd %d, wr %d", rdFormat, wrFormat);
        mStatus = BAD_VALUE;
        return;
    }
    if ((rdChannelCount != 1 && rdChannelCount != 2) ||
            wrChannelCount != 2) {
        LOGW("EchoReference cstor bad channel count rd %d, wr %d", rdChannelCount, wrChannelCount);
        mStatus = BAD_VALUE;
        return;
    }

    if (wrSamplingRate < rdSamplingRate) {
        LOGW("EchoReference cstor bad smp rate rd %d, wr %d", rdSamplingRate, wrSamplingRate);
        mStatus = BAD_VALUE;
        return;
    }

    mRdFrameSize = audio_bytes_per_sample(rdFormat) * rdChannelCount;
    mWrFrameSize = audio_bytes_per_sample(wrFormat) * wrChannelCount;

    mStatus = NO_ERROR;
}


EchoReference::~EchoReference() {
    LOGV("EchoReference dstor");
    reset_l();
    delete mDownSampler;
}

status_t EchoReference::write(EchoReference::Buffer *buffer)
{
    if (mStatus != NO_ERROR) {
        return mStatus;
    }

    AutoMutex _l(mLock);

    if (buffer == NULL) {
        LOGV("EchoReference::write() stop write");
        mState &= ~ECHOREF_WRITING;
        reset_l();
        return NO_ERROR;
    }

//    LOGV("EchoReference::write() %d frames", buffer->frameCount);

    if ((mState & ECHOREF_WRITING) == 0) {
        LOGV("EchoReference::write() start write");
        if (mDownSampler != NULL) {
            mDownSampler->reset();
        }
        mState |= ECHOREF_WRITING;
    }

    if ((mState & ECHOREF_READING) == 0) {
        return NO_ERROR;
    }


    // discard writes until a valid time stamp is provided. Once a valid TS has been received
    // reuse last good TS if none is provided.
    if (buffer->tstamp.tv_sec == 0 && buffer->tstamp.tv_nsec == 0 ) {
        if (mWrRenderTime.tv_sec == 0 && mWrRenderTime.tv_nsec == 0) {
            return NO_ERROR;
        }
    } else {
        mWrRenderTime.tv_sec = buffer->tstamp.tv_sec;
        mWrRenderTime.tv_nsec = buffer->tstamp.tv_nsec;
    }

    void *srcBuf;
    size_t inFrames;
    // do stereo to mono and down sampling if necessary
    if (mRdChannelCount != mWrChannelCount ||
            mRdSamplingRate != mWrSamplingRate) {
        if (mWrBufSize < buffer->frameCount) {
            mWrBufSize = buffer->frameCount;
            // max buffer size is normally function of read sampling rate but as write sampling rate
            // is always more than read sampling rate this works
            mWrBuf = realloc(mWrBuf, mWrBufSize * mRdFrameSize);
        }

        inFrames = buffer->frameCount;
        if (mRdChannelCount != mWrChannelCount) {
            // must be stereo to mono
            int16_t *src16 = (int16_t *)buffer->raw;
            int16_t *dst16 = (int16_t *)mWrBuf;
            size_t frames = buffer->frameCount;
            while (frames--) {
                *dst16++ = (int16_t)(((int32_t)*src16 + (int32_t)*(src16 + 1)) >> 1);
                src16 += 2;
            }
        }
        if (mWrSamplingRate != mRdSamplingRate) {
            if (mDownSampler == NULL) {
                LOGV("EchoReference::write() new ReSampler(%d, %d)", mWrSamplingRate, mRdSamplingRate);
                mDownSampler = new ReSampler(mWrSamplingRate,
                                             mRdSamplingRate,
                                             mRdChannelCount,
                                             this);

            }
            // mWrSrcBuf and mWrFramesIn are used by getNexBuffer() called by the resampler
            // to get new frames
            if (mRdChannelCount != mWrChannelCount) {
                mWrSrcBuf = mWrBuf;
            } else {
                mWrSrcBuf = buffer->raw;
            }
            mWrFramesIn = buffer->frameCount;
            // inFrames is always more than we need here to get frames remaining from previous runs
            // inFrames is updated by resample() with the number of frames produced
            mDownSampler->resample((int16_t *)mWrBuf, &inFrames);
            LOGV_IF(mWrFramesIn != 0,
                    "EchoReference::write() mWrFramesIn not 0 (%d) after resampler", mWrFramesIn);
        }
        srcBuf = mWrBuf;
    } else {
        inFrames = buffer->frameCount;
        srcBuf = buffer->raw;
    }


    if (mFramesIn + inFrames > mBufSize) {
        mBufSize = mFramesIn + inFrames;
        mBuffer = realloc(mBuffer, mBufSize * mRdFrameSize);
        LOGV("EchoReference::write() increasing buffer size to %d", mBufSize);
    }
    memcpy((char *)mBuffer + mFramesIn * mRdFrameSize,
           srcBuf,
           inFrames * mRdFrameSize);
    mFramesIn += inFrames;

    LOGV("EchoReference::write() frames %d, total frames in %d", inFrames, mFramesIn);

    mCond.signal();
    return NO_ERROR;
}

status_t EchoReference::read(EchoReference::Buffer *buffer)
{
    if (mStatus != NO_ERROR) {
        return mStatus;
    }
    AutoMutex _l(mLock);

    if (buffer == NULL) {
        LOGV("EchoReference::read() stop read");
        mState &= ~ECHOREF_READING;
        return NO_ERROR;
    }

    if ((mState & ECHOREF_READING) == 0) {
        LOGV("EchoReference::read() start read");
        reset_l();
        mState |= ECHOREF_READING;
    }

    if ((mState & ECHOREF_WRITING) == 0) {
        memset(buffer->raw, 0, mRdFrameSize * buffer->frameCount);
        buffer->tstamp.tv_sec = 0;
        buffer->tstamp.tv_nsec = 0;
        return NO_ERROR;
    }

//    LOGV("EchoReference::read() %d frames", buffer->frameCount);

    // allow some time for new frames to arrive if not enough frames are ready for read
    if (mFramesIn < buffer->frameCount) {
        uint32_t timeoutMs = (uint32_t)((1000 * buffer->frameCount) / mRdSamplingRate / 2);

        mCond.waitRelative(mLock, milliseconds(timeoutMs));
        if (mFramesIn < buffer->frameCount) {
            buffer->frameCount = mFramesIn;
            LOGV("EchoReference::read() waited %d ms but still not enough frames", timeoutMs);
        }
    }

    // computeRenderTime() must be called before subtracting frames read from mFramesIn because
    // we subtract the duration of the whole echo reference buffer including the buffer being read.
    // This is because the time stamp stored in mWrRenderTime corresponds to the last sample written
    // to the echo reference buffer and we want to return the render time of the first sample of
    // the buffer being read.
    computeRenderTime(&buffer->tstamp);

    memcpy(buffer->raw,
           (char *)mBuffer,
           buffer->frameCount * mRdFrameSize);

    mFramesIn -= buffer->frameCount;
    memcpy(mBuffer,
           (char *)mBuffer + buffer->frameCount * mRdFrameSize,
           mFramesIn * mRdFrameSize);

    LOGV("EchoReference::read() %d frames, total frames in %d", buffer->frameCount, mFramesIn);

    mCond.signal();
    return NO_ERROR;
}

void EchoReference::computeRenderTime(struct timespec *renderTime)
{
    int64_t delayNs = ((int64_t)mFramesIn * 1000000000) / mRdSamplingRate;

    if (mDownSampler != NULL) {
        delayNs += mDownSampler->delayNs();
    }

    struct timespec tmp;
    tmp.tv_nsec = delayNs % 1000000000;
    tmp.tv_sec = delayNs / 1000000000;

    if (mWrRenderTime.tv_nsec < tmp.tv_nsec)
    {
        renderTime->tv_sec = mWrRenderTime.tv_sec - tmp.tv_sec - 1;
        renderTime->tv_nsec = 1000000000 + mWrRenderTime.tv_nsec - tmp.tv_nsec;
    } else {
        renderTime->tv_sec = mWrRenderTime.tv_sec - tmp.tv_sec;
        renderTime->tv_nsec = mWrRenderTime.tv_nsec - tmp.tv_nsec;
    }
}

void EchoReference::reset_l() {
    LOGV("EchoReference::reset_l()");
    free(mBuffer);
    mBuffer = NULL;
    mBufSize = 0;
    mFramesIn = 0;
    free(mWrBuf);
    mWrBuf = NULL;
    mWrBufSize = 0;
    mWrRenderTime.tv_sec = 0;
    mWrRenderTime.tv_nsec = 0;
}

status_t EchoReference::getNextBuffer(ReSampler::BufferProvider::Buffer* buffer)
{
    if (mWrSrcBuf == NULL || mWrFramesIn == 0) {
        buffer->raw = NULL;
        buffer->frameCount = 0;
        return NOT_ENOUGH_DATA;
    }

    buffer->frameCount = (buffer->frameCount > mWrFramesIn) ? mWrFramesIn : buffer->frameCount;
    // this is mRdChannelCount here as we resample after stereo to mono conversion if any
    buffer->i16 = (int16_t *)mWrSrcBuf + (mWrBufSize - mWrFramesIn) * mRdChannelCount;

    return 0;
}

void EchoReference::releaseBuffer(ReSampler::BufferProvider::Buffer* buffer)
{
    mWrFramesIn -= buffer->frameCount;
}

}; // namespace android_audio_legacy
