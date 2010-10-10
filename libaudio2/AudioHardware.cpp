/*
** Copyright 2010, The Android Open-Source Project
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

#include <math.h>

#define LOG_TAG "AudioHardware"

#include <utils/Log.h>
#include <utils/String8.h>

#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <fcntl.h>

#include "AudioHardware.h"
#include <media/AudioRecord.h>

extern "C" {
#include "alsa_audio.h"
}


namespace android {
// ----------------------------------------------------------------------------

AudioHardware::AudioHardware() :
    mInit(false), mMicMute(true), mOutput(0)
{
    mInit = true;
}

AudioHardware::~AudioHardware()
{
    closeOutputStream((AudioStreamOut*)mOutput);
    mInit = false;
}

status_t AudioHardware::initCheck()
{
    return mInit ? NO_ERROR : NO_INIT;
}

AudioStreamOut* AudioHardware::openOutputStream(
    uint32_t devices, int *format, uint32_t *channels,
    uint32_t *sampleRate, status_t *status)
{
    { // scope for the lock
        Mutex::Autolock lock(mLock);

        // only one output stream allowed
        if (mOutput) {
            if (status) {
                *status = INVALID_OPERATION;
            }
            return 0;
        }

        AudioStreamOutALSA* out = new AudioStreamOutALSA();

        status_t rc = out->set(this, devices, format, channels, sampleRate);
        if (rc) {
            *status = rc;
        }
        if (rc == NO_ERROR) {
            mOutput = out;
        } else {
            delete out;
        }
    }
    return mOutput;
}

void AudioHardware::closeOutputStream(AudioStreamOut* out) {
    Mutex::Autolock lock(mLock);
    if (mOutput == 0 || mOutput != out) {
        LOGW("Attempt to close invalid output stream");
    } else {
        delete mOutput;
        mOutput = 0;
    }
}

AudioStreamIn* AudioHardware::openInputStream(
    uint32_t devices, int *format, uint32_t *channels,
    uint32_t *sampleRate, status_t *status,
    AudioSystem::audio_in_acoustics acoustic_flags)
{
    return 0;
}

void AudioHardware::closeInputStream(AudioStreamIn* in) {
}

status_t AudioHardware::setMode(int mode)
{
    return NO_ERROR;
}

status_t AudioHardware::setMicMute(bool state)
{
    return NO_ERROR;
}

status_t AudioHardware::getMicMute(bool* state)
{
    *state = mMicMute;
    return NO_ERROR;
}

status_t AudioHardware::setParameters(const String8& keyValuePairs)
{
    return NO_ERROR;
}

String8 AudioHardware::getParameters(const String8& keys)
{
    AudioParameter request = AudioParameter(keys);
    AudioParameter reply = AudioParameter();

    LOGV("getParameters() %s", keys.string());

    return reply.toString();
}

size_t AudioHardware::getInputBufferSize(uint32_t sampleRate, int format, int channelCount)
{
    return 4096;
}

status_t AudioHardware::setVoiceVolume(float v)
{
    return NO_ERROR;
}

status_t AudioHardware::setMasterVolume(float v)
{
    LOGI("Set master volume to %f.\n", v);
    // We return an error code here to let the audioflinger do in-software
    // volume on top of the maximum volume that we set through the SND API.
    // return error - software mixer will handle it
    return -1;
}

status_t AudioHardware::dump(int fd, const Vector<String16>& args)
{
    return NO_ERROR;
}

AudioHardware::AudioStreamOutALSA::AudioStreamOutALSA() :
    mHardware(0), mPcm(0), mMixer(0), mRouteCtl(0), mStartCount(0), mRetryCount(0),
    mStandby(true), mDevices(0), mChannels(AUDIO_HW_OUT_CHANNELS),
    mSampleRate(AUDIO_HW_OUT_SAMPLERATE), mBufferSize(AUDIO_HW_OUT_BUFSZ)
{
}

status_t AudioHardware::AudioStreamOutALSA::set(
    AudioHardware* hw, uint32_t devices, int *pFormat,
    uint32_t *pChannels, uint32_t *pRate)
{
    int lFormat = pFormat ? *pFormat : 0;
    uint32_t lChannels = pChannels ? *pChannels : 0;
    uint32_t lRate = pRate ? *pRate : 0;

    mHardware = hw;
    mDevices = devices;

    // fix up defaults
    if (lFormat == 0) lFormat = format();
    if (lChannels == 0) lChannels = channels();
    if (lRate == 0) lRate = sampleRate();

    // check values
    if ((lFormat != format()) ||
        (lChannels != channels()) ||
        (lRate != sampleRate())) {
        if (pFormat) *pFormat = format();
        if (pChannels) *pChannels = channels();
        if (pRate) *pRate = sampleRate();
        return BAD_VALUE;
    }

    if (pFormat) *pFormat = lFormat;
    if (pChannels) *pChannels = lChannels;
    if (pRate) *pRate = lRate;

    mChannels = lChannels;
    mSampleRate = lRate;
    mBufferSize = 4096;

    return NO_ERROR;
}

AudioHardware::AudioStreamOutALSA::~AudioStreamOutALSA()
{
    standby();
}

ssize_t AudioHardware::AudioStreamOutALSA::write(const void* buffer, size_t bytes)
{
    // LOGD("AudioStreamOutALSA::write(%p, %u)", buffer, bytes);
    status_t status = NO_INIT;
    const uint8_t* p = static_cast<const uint8_t*>(buffer);
    int ret;

    if (mStandby) {
        LOGV("open pcm_out driver");

        next_route = "SPK";

        mPcm = pcm_open(PCM_OUT);
        if (!pcm_ready(mPcm)) {
            LOGE("cannot open pcm_out driver: %s\n", pcm_error(mPcm));
            pcm_close(mPcm);
            mPcm = 0;
            goto Error;
        }

        mMixer = mixer_open();
        if (mMixer)
            mRouteCtl = mixer_get_control(mMixer, "Playback Path", 0);

#if 0
        LOGV("set pcm_out config");
        config.channel_count = AudioSystem::popCount(channels());
        config.sample_rate = mSampleRate;
        config.buffer_size = mBufferSize;
        config.buffer_count = AUDIO_HW_NUM_OUT_BUF;
//        config.codec_type = CODEC_TYPE_PCM;
        status = ioctl(mFd, AUDIO_SET_CONFIG, &config);
        if (status < 0) {
            LOGE("Cannot set config");
            goto Error;
        }

        LOGV("buffer_size: %u", config.buffer_size);
        LOGV("buffer_count: %u", config.buffer_count);
        LOGV("channel_count: %u", config.channel_count);
        LOGV("sample_rate: %u", config.sample_rate);
#endif

        mStandby = false;
    }

    ret = pcm_write(mPcm,(void*) p, bytes);
    if (ret == 0) {
        if (next_route && mRouteCtl) {
            mixer_ctl_select(mRouteCtl, next_route);
            next_route = 0;
        }
        return bytes;
    }
    LOGW("write error: %d", errno);
    status = -errno;

Error:
    standby();

    // Simulate audio output timing in case of error
    usleep(bytes * 1000000 / frameSize() / sampleRate());

    return status;
}

status_t AudioHardware::AudioStreamOutALSA::standby()
{
    if (mPcm) {
        pcm_close(mPcm);
        mPcm = 0;
    }
    if (mMixer) {
        mixer_close(mMixer);
        mMixer = 0;
        if (mRouteCtl) {
            mRouteCtl = 0;
        }
    }
    mStandby = true;
    LOGI("AudioHardware pcm playback is going to standby.");
    return NO_ERROR;
}

status_t AudioHardware::AudioStreamOutALSA::dump(int fd, const Vector<String16>& args)
{
    return NO_ERROR;
}

bool AudioHardware::AudioStreamOutALSA::checkStandby()
{
    return mStandby;
}

status_t AudioHardware::AudioStreamOutALSA::setParameters(const String8& keyValuePairs)
{
    return NO_ERROR;
}

String8 AudioHardware::AudioStreamOutALSA::getParameters(const String8& keys)
{
    AudioParameter param = AudioParameter(keys);
    LOGV("AudioStreamOutALSA::getParameters() %s", param.toString().string());
    return param.toString();
}

status_t AudioHardware::AudioStreamOutALSA::getRenderPosition(uint32_t *dspFrames)
{
    return INVALID_OPERATION;
}

extern "C" AudioHardwareInterface* createAudioHardware(void) {
    return new AudioHardware();
}

}; // namespace android
