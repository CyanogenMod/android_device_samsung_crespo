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

#ifndef ANDROID_ECHO_REFERENCE_H
#define ANDROID_ECHO_REFERENCE_H

#include <stdint.h>
#include <sys/types.h>
#include <utils/threads.h>
#include <hardware_legacy/AudioSystemLegacy.h>
#include "ReSampler.h"

namespace android_audio_legacy {
using android::Mutex;
using android::AutoMutex;

class EchoReference : public ReSampler::BufferProvider {

public:

    EchoReference(audio_format_t rdFormat,
                  uint32_t rdChannelCount,
                  uint32_t rdSamplingRate,
                  audio_format_t wrFormat,
                  uint32_t wrChannelCount,
                  uint32_t wrSamplingRate);
    virtual ~EchoReference();

    // echo reference state: it field indicating if read, write or both are active.
    enum state {
        ECHOREF_IDLE = 0x00,        // idle
        ECHOREF_READING = 0x01,     // reading is active
        ECHOREF_WRITING = 0x02      // writing is active
    };

    // Buffer descriptor used by read() and write() methods, including the render time stamp.
    // for write(), The time stamp is the render time for the last sample in the buffer written
    // for read(), the time stamp is the render time for the first sample in the buffer read
    class Buffer {
     public:
        void *raw;
        size_t frameCount;
        struct timespec tstamp;     // render time stamp of buffer. 0.0 means if unknown
    };

    // BufferProvider
    status_t getNextBuffer(ReSampler::BufferProvider::Buffer* buffer);
    void releaseBuffer(ReSampler::BufferProvider::Buffer* buffer);

    status_t initCheck() { return mStatus; }

    status_t read(Buffer *buffer);
    status_t write(Buffer *buffer);

private:

    void reset_l();
    void computeRenderTime(struct timespec *renderTime);

    status_t mStatus;               // init status
    uint32_t mState;                // active state: reading, writing or both
    audio_format_t mRdFormat;       // read sample format
    uint32_t mRdChannelCount;       // read number of channels
    uint32_t mRdSamplingRate;       // read sampling rate
    size_t mRdFrameSize;            // read frame size (bytes per sample)
    audio_format_t mWrFormat;       // write sample format
    uint32_t mWrChannelCount;       // write number of channels
    uint32_t mWrSamplingRate;       // write sampling rate
    size_t mWrFrameSize;            // write frame size (bytes per sample)
    void *mBuffer;                  // main buffer
    size_t mBufSize;                // main buffer size in frames
    size_t mFramesIn;               // number of frames in main buffer
    void *mWrBuf;                   // buffer for input conversions
    size_t mWrBufSize;              // size of conversion buffer in frames
    size_t mWrFramesIn;             // number of frames in conversion buffer
    void *mWrSrcBuf;                // resampler input buf (either mWrBuf or buffer used by write()
    struct timespec mWrRenderTime;  // latest render time indicated by write()
    uint32_t mRdDurationUs;         // Duration of last buffer read (used for mCond wait timeout)
    android::Mutex   mLock;         // Mutex protecting read/write concurrency
    android::Condition mCond;       // Condition signaled when data is ready to read
    ReSampler *mDownSampler;        // input resampler
};

}; // namespace android

#endif // ANDROID_ECHO_REFERENCE_H
