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

status_t EchoReference::write(Buffer *buffer)
{
    if (mStatus != NO_ERROR) {
        LOGV("EchoReference::write() ERROR, exiting early");
        return mStatus;
    }

    AutoMutex _l(mLock);

    if (buffer == NULL) {
        LOGV("EchoReference::write() stop write");
        mState &= ~ECHOREF_WRITING;
        reset_l();
        return NO_ERROR;
    }

    LOGV("EchoReference::write() START trying to write %d frames", buffer->frameCount);
    LOGV("EchoReference::write() playbackTimestamp:[%lld].[%lld], mPlaybackDelay:[%ld]",
    (int64_t)buffer->timeStamp.tv_sec,
    (int64_t)buffer->timeStamp.tv_nsec, mPlaybackDelay);

    //LOGV("EchoReference::write() %d frames", buffer->frameCount);
    // discard writes until a valid time stamp is provided.

    if ((buffer->timeStamp.tv_sec == 0) && (buffer->timeStamp.tv_nsec == 0) &&
        (mWrRenderTime.tv_sec     == 0) && (mWrRenderTime.tv_nsec     == 0)) {
            return NO_ERROR;
    }

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

    mWrRenderTime.tv_sec  = buffer->timeStamp.tv_sec;
    mWrRenderTime.tv_nsec = buffer->timeStamp.tv_nsec;

    mPlaybackDelay = buffer->delayNs;

    void *srcBuf;
    size_t inFrames;
    // do stereo to mono and down sampling if necessary
    if (mRdChannelCount != mWrChannelCount ||
            mRdSamplingRate != mWrSamplingRate) {
        if (mWrBufSize < buffer->frameCount) {
            mWrBufSize = buffer->frameCount;
            //max buffer size is normally function of read sampling rate but as write sampling rate
            //is always more than read sampling rate this works
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
                LOGV("EchoReference::write() new ReSampler(%d, %d)",
                      mWrSamplingRate, mRdSamplingRate);
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
            LOGV("EchoReference::write() ReSampling(%d, %d)",
                  mWrSamplingRate, mRdSamplingRate);
            mDownSampler->resample((int16_t *)mWrBuf, &inFrames);
            LOGV_IF(mWrFramesIn != 0,
                    "EchoReference::write() mWrFramesIn not 0 (%d) after resampler",
                    mWrFramesIn);
        }
        srcBuf = mWrBuf;
    } else {
        inFrames = buffer->frameCount;
        srcBuf = buffer->raw;
    }

    if (mFramesIn + inFrames > mBufSize) {
        LOGV("EchoReference::write() increasing buffer size from %d to %d",
        mBufSize, mFramesIn + inFrames);
        mBufSize = mFramesIn + inFrames;
        mBuffer = realloc(mBuffer, mBufSize * mRdFrameSize);
    }
    memcpy((char *)mBuffer + mFramesIn * mRdFrameSize,
           srcBuf,
           inFrames * mRdFrameSize);
    mFramesIn += inFrames;

    LOGV("EchoReference::write_log() inFrames:[%d], mFramesInOld:[%d], "\
         "mFramesInNew:[%d], mBufSize:[%d], mWrRenderTime:[%lld].[%lld], mPlaybackDelay:[%ld]",
         inFrames, mFramesIn - inFrames, mFramesIn, mBufSize,  (int64_t)mWrRenderTime.tv_sec,
         (int64_t)mWrRenderTime.tv_nsec, mPlaybackDelay);

    mCond.signal();
    LOGV("EchoReference::write() END");
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

    LOGV("EchoReference::read() START, delayCapture:[%ld],mFramesIn:[%d],buffer->frameCount:[%d]",
    buffer->delayNs, mFramesIn, buffer->frameCount);

    if ((mState & ECHOREF_READING) == 0) {
        LOGV("EchoReference::read() start read");
        reset_l();
        mState |= ECHOREF_READING;
    }

    if ((mState & ECHOREF_WRITING) == 0) {
        memset(buffer->raw, 0, mRdFrameSize * buffer->frameCount);
        buffer->delayNs = 0;
        return NO_ERROR;
    }

//    LOGV("EchoReference::read() %d frames", buffer->frameCount);

    // allow some time for new frames to arrive if not enough frames are ready for read
    if (mFramesIn < buffer->frameCount) {
        uint32_t timeoutMs = (uint32_t)((1000 * buffer->frameCount) / mRdSamplingRate / 2);

        mCond.waitRelative(mLock, milliseconds(timeoutMs));
        if (mFramesIn < buffer->frameCount) {
            LOGV("EchoReference::read() waited %d ms but still not enough frames"\
                 " mFramesIn: %d, buffer->frameCount = %d",
                timeoutMs, mFramesIn, buffer->frameCount);
            buffer->frameCount = mFramesIn;
        }
    }

    int64_t timeDiff;
    struct timespec tmp;

    if ((mWrRenderTime.tv_sec == 0 && mWrRenderTime.tv_nsec == 0) ||
        (buffer->timeStamp.tv_sec == 0 && buffer->timeStamp.tv_nsec == 0)) {
        LOGV("read: NEW:timestamp is zero---------setting timeDiff = 0, "\
             "not updating delay this time");
        timeDiff = 0;
    } else {
        if (buffer->timeStamp.tv_nsec < mWrRenderTime.tv_nsec) {
            tmp.tv_sec = buffer->timeStamp.tv_sec - mWrRenderTime.tv_sec - 1;
            tmp.tv_nsec = 1000000000 + buffer->timeStamp.tv_nsec - mWrRenderTime.tv_nsec;
        } else {
            tmp.tv_sec = buffer->timeStamp.tv_sec - mWrRenderTime.tv_sec;
            tmp.tv_nsec = buffer->timeStamp.tv_nsec - mWrRenderTime.tv_nsec;
        }
        timeDiff = (((int64_t)tmp.tv_sec * 1000000000 + tmp.tv_nsec));

        int64_t expectedDelayNs =  mPlaybackDelay + buffer->delayNs - timeDiff;

        LOGV("expectedDelayNs[%lld] =  mPlaybackDelay[%ld] + delayCapture[%ld] - timeDiff[%lld]",
        expectedDelayNs, mPlaybackDelay, buffer->delayNs, timeDiff);

        if (expectedDelayNs > 0) {
            int64_t delayNs = ((int64_t)mFramesIn * 1000000000) / mRdSamplingRate;

            delayNs -= expectedDelayNs;

            if (abs(delayNs) >= sMinDelayUpdate) {
                if (delayNs < 0) {
                    size_t previousFrameIn = mFramesIn;
                    mFramesIn = (expectedDelayNs * mRdSamplingRate)/1000000000;
                    int    offset = mFramesIn - previousFrameIn;
                    LOGV("EchoReference::readlog: delayNs = NEGATIVE and ENOUGH : "\
                         "setting %d frames to zero mFramesIn: %d, previousFrameIn = %d",
                         offset, mFramesIn, previousFrameIn);

                    if (mFramesIn > mBufSize) {
                        mBufSize = mFramesIn;
                        mBuffer  = realloc(mBuffer, mFramesIn * mRdFrameSize);
                        LOGV("EchoReference::read: increasing buffer size to %d", mBufSize);
                    }

                    if (offset > 0)
                        memset((char *)mBuffer + previousFrameIn * mRdFrameSize,
                               0, offset * mRdFrameSize);
                } else {
                    size_t  previousFrameIn = mFramesIn;
                    int     framesInInt = (int)(((int64_t)expectedDelayNs *
                                           (int64_t)mRdSamplingRate)/1000000000);
                    int     offset = previousFrameIn - framesInInt;

                    LOGV("EchoReference::readlog: delayNs = POSITIVE/ENOUGH :previousFrameIn: %d,"\
                         "framesInInt: [%d], offset:[%d], buffer->frameCount:[%d]",
                         previousFrameIn, framesInInt, offset, buffer->frameCount);

                    if (framesInInt < (int)buffer->frameCount) {
                        if (framesInInt > 0) {
                            memset((char *)mBuffer + framesInInt * mRdFrameSize,
                                   0, (buffer->frameCount-framesInInt) * mRdFrameSize);
                            LOGV("EchoReference::read: pushing [%d] zeros into ref buffer",
                                 (buffer->frameCount-framesInInt));
                        } else {
                            LOGV("framesInInt = %d", framesInInt);
                        }
                        framesInInt = buffer->frameCount;
                    } else {
                        if (offset > 0) {
                            memcpy(mBuffer, (char *)mBuffer + (offset * mRdFrameSize),
                                   framesInInt * mRdFrameSize);
                            LOGV("EchoReference::read: shifting ref buffer by [%d]",framesInInt);
                        }
                    }
                    mFramesIn = (size_t)framesInInt;
                }
            } else {
                LOGV("EchoReference::read: NOT ENOUGH samples to update %lld", delayNs);
            }
        } else {
            LOGV("NEGATIVE expectedDelayNs[%lld] =  "\
                 "mPlaybackDelay[%ld] + delayCapture[%ld] - timeDiff[%lld]",
                 expectedDelayNs, mPlaybackDelay, buffer->delayNs, timeDiff);
        }
    }

    memcpy(buffer->raw,
           (char *)mBuffer,
           buffer->frameCount * mRdFrameSize);

    mFramesIn -= buffer->frameCount;
    memcpy(mBuffer,
           (char *)mBuffer + buffer->frameCount * mRdFrameSize,
           mFramesIn * mRdFrameSize);

    // As the reference buffer is now time aligned to the microphone signal there is a zero delay
    buffer->delayNs = 0;

    LOGV("EchoReference::read() END %d frames, total frames in %d",
          buffer->frameCount, mFramesIn);

    mCond.signal();
    return NO_ERROR;
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
