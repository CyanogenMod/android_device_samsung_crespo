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

#ifndef ANDROID_AUDIO_HARDWARE_H
#define ANDROID_AUDIO_HARDWARE_H

#include <stdint.h>
#include <sys/types.h>

#include <utils/threads.h>
#include <utils/SortedVector.h>

#include <hardware_legacy/AudioHardwareBase.h>

extern "C" {
    struct pcm;
    struct mixer;
    struct mixer_ctl;
};

namespace android {

#define CODEC_TYPE_PCM 0
#define PCM_FILL_BUFFER_COUNT 1
// Number of buffers in audio driver for output
#define AUDIO_HW_NUM_OUT_BUF 2

// TODO: determine actual audio DSP and hardware latency
// Additionnal latency introduced by audio DSP and hardware in ms
#define AUDIO_HW_OUT_LATENCY_MS 0
// Default audio output sample rate
#define AUDIO_HW_OUT_SAMPLERATE 44100
// Default audio output channel mask
#define AUDIO_HW_OUT_CHANNELS (AudioSystem::CHANNEL_OUT_STEREO)
// Default audio output sample format
#define AUDIO_HW_OUT_FORMAT (AudioSystem::PCM_16_BIT)
// Default audio output buffer size
#define AUDIO_HW_OUT_BUFSZ 4096

#if 0
// Default audio input sample rate
#define AUDIO_HW_IN_SAMPLERATE 8000
// Default audio input channel mask
#define AUDIO_HW_IN_CHANNELS (AudioSystem::CHANNEL_IN_MONO)
// Default audio input sample format
#define AUDIO_HW_IN_FORMAT (AudioSystem::PCM_16_BIT)
// Default audio input buffer size
#define AUDIO_HW_IN_BUFSZ 256

// Maximum voice volume
#define VOICE_VOLUME_MAX 5
#endif

class AudioHardware : public AudioHardwareBase
{
    class AudioStreamOutALSA;
public:
    AudioHardware();
    virtual ~AudioHardware();
    virtual status_t initCheck();

    virtual status_t setVoiceVolume(float volume);
    virtual status_t setMasterVolume(float volume);

    virtual status_t setMode(int mode);

    virtual status_t setMicMute(bool state);
    virtual status_t getMicMute(bool* state);

    virtual status_t setParameters(const String8& keyValuePairs);
    virtual String8 getParameters(const String8& keys);

    virtual AudioStreamOut* openOutputStream(
        uint32_t devices, int *format=0, uint32_t *channels=0,
        uint32_t *sampleRate=0, status_t *status=0);

    virtual AudioStreamIn* openInputStream(
        uint32_t devices, int *format, uint32_t *channels,
        uint32_t *sampleRate, status_t *status,
        AudioSystem::audio_in_acoustics acoustics);

    virtual void closeOutputStream(AudioStreamOut* out);
    virtual void closeInputStream(AudioStreamIn* in);

    virtual size_t getInputBufferSize(
        uint32_t sampleRate, int format, int channelCount);

    void clearCurDevice() { }

protected:
    virtual status_t dump(int fd, const Vector<String16>& args);

private:
    bool mInit;
    bool  mMicMute;
    AudioStreamOutALSA *mOutput;
    Mutex mLock;
    struct mixer *mMixer;

    class AudioStreamOutALSA : public AudioStreamOut
    {
    public:
        AudioStreamOutALSA();
        virtual ~AudioStreamOutALSA();
        status_t set(AudioHardware* mHardware,
                     uint32_t devices,
                     int *pFormat,
                     uint32_t *pChannels,
                     uint32_t *pRate);
        virtual uint32_t sampleRate()
            const { return mSampleRate; }
        virtual size_t bufferSize()
            const { return mBufferSize; }
        virtual uint32_t channels()
            const { return mChannels; }
        virtual int format()
            const { return AUDIO_HW_OUT_FORMAT; }
        virtual uint32_t latency()
            const { return (1000 * AUDIO_HW_NUM_OUT_BUF *
                            (bufferSize()/frameSize()))/sampleRate() +
                AUDIO_HW_OUT_LATENCY_MS; }
        virtual status_t setVolume(float left, float right)
        { return INVALID_OPERATION; }
        virtual ssize_t write(const void* buffer, size_t bytes);
        virtual status_t standby();
        virtual status_t dump(int fd, const Vector<String16>& args);
        bool checkStandby();
        virtual status_t setParameters(const String8& keyValuePairs);
        virtual String8 getParameters(const String8& keys);
        uint32_t devices()
        { return mDevices; }
        virtual status_t getRenderPosition(uint32_t *dspFrames);

    private:
        AudioHardware* mHardware;
        struct pcm *mPcm;
        struct mixer *mMixer;
        struct mixer_ctl *mRouteCtl;
        const char *next_route;
        int mStartCount;
        int mRetryCount;
        bool mStandby;
        uint32_t mDevices;
        uint32_t mChannels;
        uint32_t mSampleRate;
        size_t mBufferSize;
    };
};

}; // namespace android

#endif
