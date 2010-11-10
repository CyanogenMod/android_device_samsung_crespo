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

//#define LOG_NDEBUG 0

#define LOG_TAG "AudioHardware"

#include <utils/Log.h>
#include <utils/String8.h>

#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <dlfcn.h>
#include <fcntl.h>

#include "AudioHardware.h"
#include <media/AudioRecord.h>
#include <hardware_legacy/power.h>

extern "C" {
#include "alsa_audio.h"
}


namespace android {

const uint32_t AudioHardware::inputSamplingRates[] = {
        8000, 11025, 16000, 22050, 44100
};

//  trace driver operations for dump
//
#define DRIVER_TRACE

enum {
    DRV_NONE,
    DRV_PCM_OPEN,
    DRV_PCM_CLOSE,
    DRV_PCM_WRITE,
    DRV_PCM_READ,
    DRV_MIXER_OPEN,
    DRV_MIXER_CLOSE,
    DRV_MIXER_GET,
    DRV_MIXER_SEL
};

#ifdef DRIVER_TRACE
#define TRACE_DRIVER_IN(op) mDriverOp = op;
#define TRACE_DRIVER_OUT mDriverOp = DRV_NONE;
#else
#define TRACE_DRIVER_IN(op)
#define TRACE_DRIVER_OUT
#endif

// ----------------------------------------------------------------------------

AudioHardware::AudioHardware() :
    mInit(false),
    mMicMute(false),
    mPcm(NULL),
    mMixer(NULL),
    mPcmOpenCnt(0),
    mMixerOpenCnt(0),
    mInCallAudioMode(false),
    mInputSource("Default"),
    mBluetoothNrec(true),
    mSecRilLibHandle(NULL),
    mRilClient(0),
    mActivatedCP(false),
    mDriverOp(DRV_NONE)
{
    loadRILD();
    mInit = true;
}

AudioHardware::~AudioHardware()
{
    for (size_t index = 0; index < mInputs.size(); index++) {
        closeInputStream(mInputs[index].get());
    }
    mInputs.clear();
    closeOutputStream((AudioStreamOut*)mOutput.get());

    if (mMixer) {
        TRACE_DRIVER_IN(DRV_MIXER_CLOSE)
        mixer_close(mMixer);
        TRACE_DRIVER_OUT
    }
    if (mPcm) {
        TRACE_DRIVER_IN(DRV_PCM_CLOSE)
        pcm_close(mPcm);
        TRACE_DRIVER_OUT
    }

    if (mSecRilLibHandle) {
        if (disconnectRILD(mRilClient) != RIL_CLIENT_ERR_SUCCESS)
            LOGE("Disconnect_RILD() error");

        if (closeClientRILD(mRilClient) != RIL_CLIENT_ERR_SUCCESS)
            LOGE("CloseClient_RILD() error");

        mRilClient = 0;

        dlclose(mSecRilLibHandle);
        mSecRilLibHandle = NULL;
    }

    mInit = false;
}

status_t AudioHardware::initCheck()
{
    return mInit ? NO_ERROR : NO_INIT;
}

void AudioHardware::loadRILD(void)
{
    mSecRilLibHandle = dlopen("libsecril-client.so", RTLD_NOW);

    if (mSecRilLibHandle) {
        LOGV("libsecril-client.so is loaded");

        openClientRILD   = (HRilClient (*)(void))
                              dlsym(mSecRilLibHandle, "OpenClient_RILD");
        disconnectRILD   = (int (*)(HRilClient))
                              dlsym(mSecRilLibHandle, "Disconnect_RILD");
        closeClientRILD  = (int (*)(HRilClient))
                              dlsym(mSecRilLibHandle, "CloseClient_RILD");
        isConnectedRILD  = (int (*)(HRilClient))
                              dlsym(mSecRilLibHandle, "isConnected_RILD");
        connectRILD      = (int (*)(HRilClient))
                              dlsym(mSecRilLibHandle, "Connect_RILD");
        setCallVolume    = (int (*)(HRilClient, SoundType, int))
                              dlsym(mSecRilLibHandle, "SetCallVolume");
        setCallAudioPath = (int (*)(HRilClient, AudioPath))
                              dlsym(mSecRilLibHandle, "SetCallAudioPath");
        setCallClockSync = (int (*)(HRilClient, SoundClockCondition))
                              dlsym(mSecRilLibHandle, "SetCallClockSync");

        if (!openClientRILD  || !disconnectRILD   || !closeClientRILD ||
            !isConnectedRILD || !connectRILD      ||
            !setCallVolume   || !setCallAudioPath || !setCallClockSync) {
            LOGE("Can't load all functions from libsecril-client.so");

            dlclose(mSecRilLibHandle);
            mSecRilLibHandle = NULL;
        } else {
            mRilClient = openClientRILD();
            if (!mRilClient) {
                LOGE("OpenClient_RILD() error");

                dlclose(mSecRilLibHandle);
                mSecRilLibHandle = NULL;
            }
        }
    } else {
        LOGE("Can't load libsecril-client.so");
    }
}

status_t AudioHardware::connectRILDIfRequired(void)
{
    if (!mSecRilLibHandle) {
        LOGE("connectIfRequired() lib is not loaded");
        return INVALID_OPERATION;
    }

    if (isConnectedRILD(mRilClient)) {
        return OK;
    }

    if (connectRILD(mRilClient) != RIL_CLIENT_ERR_SUCCESS) {
        LOGE("Connect_RILD() error");
        return INVALID_OPERATION;
    }

    return OK;
}

AudioStreamOut* AudioHardware::openOutputStream(
    uint32_t devices, int *format, uint32_t *channels,
    uint32_t *sampleRate, status_t *status)
{
    sp <AudioStreamOutALSA> out;
    status_t rc;

    { // scope for the lock
        Mutex::Autolock lock(mLock);

        // only one output stream allowed
        if (mOutput != 0) {
            if (status) {
                *status = INVALID_OPERATION;
            }
            return NULL;
        }

        out = new AudioStreamOutALSA();

        rc = out->set(this, devices, format, channels, sampleRate);
        if (rc == NO_ERROR) {
            mOutput = out;
        }
    }

    if (rc != NO_ERROR) {
        if (out != 0) {
            out.clear();
        }
    }
    if (status) {
        *status = rc;
    }

    return out.get();
}

void AudioHardware::closeOutputStream(AudioStreamOut* out) {
    sp <AudioStreamOutALSA> spOut;
    {
        Mutex::Autolock lock(mLock);
        if (mOutput == 0 || mOutput.get() != out) {
            LOGW("Attempt to close invalid output stream");
            return;
        }
        spOut = mOutput;
        mOutput.clear();
    }
    spOut.clear();
}

AudioStreamIn* AudioHardware::openInputStream(
    uint32_t devices, int *format, uint32_t *channels,
    uint32_t *sampleRate, status_t *status,
    AudioSystem::audio_in_acoustics acoustic_flags)
{
    // check for valid input source
    if (!AudioSystem::isInputDevice((AudioSystem::audio_devices)devices)) {
        if (status) {
            *status = BAD_VALUE;
        }
        return NULL;
    }

    status_t rc = NO_ERROR;
    sp <AudioStreamInALSA> in;

    { // scope for the lock
        Mutex::Autolock lock(mLock);

        in = new AudioStreamInALSA();
        rc = in->set(this, devices, format, channels, sampleRate, acoustic_flags);
        if (rc == NO_ERROR) {
            mInputs.add(in);
        }
    }

    if (rc != NO_ERROR) {
        if (in != 0) {
            in.clear();
        }
    }
    if (status) {
        *status = rc;
    }

    LOGV("AudioHardware::openInputStream()%p", in.get());
    return in.get();
}

void AudioHardware::closeInputStream(AudioStreamIn* in) {

    sp<AudioStreamInALSA> spIn;
    {
        Mutex::Autolock lock(mLock);

        ssize_t index = mInputs.indexOf((AudioStreamInALSA *)in);
        if (index < 0) {
            LOGW("Attempt to close invalid input stream");
            return;
        }
        spIn = mInputs[index];
        mInputs.removeAt(index);
    }
    LOGV("AudioHardware::closeInputStream()%p", in);
    spIn.clear();
}


status_t AudioHardware::setMode(int mode)
{
    sp<AudioStreamOutALSA> spOut;
    sp<AudioStreamInALSA> spIn;
    status_t status;

    // bump thread priority to speed up mutex acquisition
    int  priority = getpriority(PRIO_PROCESS, 0);
    setpriority(PRIO_PROCESS, 0, ANDROID_PRIORITY_URGENT_AUDIO);

    // Mutex acquisition order is always out -> in -> hw
    AutoMutex lock(mLock);

    spOut = mOutput;
    while (spOut != 0) {
        if (!spOut->checkStandby()) {
            int cnt = spOut->standbyCnt();
            mLock.unlock();
            spOut->lock();
            mLock.lock();
            // make sure that another thread did not change output state while the
            // mutex is released
            if ((spOut == mOutput) && (cnt == spOut->standbyCnt())) {
                break;
            }
            spOut->unlock();
            spOut = mOutput;
        } else {
            spOut.clear();
        }
    }
    // spOut is not 0 here only if the output is active

    spIn = getActiveInput_l();
    while (spIn != 0) {
        int cnt = spIn->standbyCnt();
        mLock.unlock();
        spIn->lock();
        mLock.lock();
        // make sure that another thread did not change input state while the
        // mutex is released
        if ((spIn == getActiveInput_l()) && (cnt == spIn->standbyCnt())) {
            break;
        }
        spIn->unlock();
        spIn = getActiveInput_l();
    }
    // spIn is not 0 here only if the input is active

    setpriority(PRIO_PROCESS, 0, priority);

    int prevMode = mMode;
    status = AudioHardwareBase::setMode(mode);
    LOGV("setMode() : new %d, old %d", mMode, prevMode);
    if (status == NO_ERROR) {
        // activate call clock in radio when entering in call or ringtone mode
        if (prevMode == AudioSystem::MODE_NORMAL)
        {
            if ((!mActivatedCP) && (mSecRilLibHandle) && (connectRILDIfRequired() == OK)) {
                setCallClockSync(mRilClient, SOUND_CLOCK_START);
                mActivatedCP = true;
            }
        }

        if (mMode == AudioSystem::MODE_IN_CALL && !mInCallAudioMode) {
            if (spOut != 0) {
                LOGV("setMode() in call force output standby");
                spOut->doStandby_l();
            }
            if (spIn != 0) {
                LOGV("setMode() in call force input standby");
                spIn->doStandby_l();
            }

            LOGV("setMode() openPcmOut_l()");
            openPcmOut_l();
            openMixer_l();
            setInputSource_l(String8("Default"));
            mInCallAudioMode = true;
        }
        if (mMode == AudioSystem::MODE_NORMAL && mInCallAudioMode) {
            setInputSource_l(mInputSource);
            if (mMixer != NULL) {
                TRACE_DRIVER_IN(DRV_MIXER_GET)
                struct mixer_ctl *ctl= mixer_get_control(mMixer, "Playback Path", 0);
                TRACE_DRIVER_OUT
                if (ctl != NULL) {
                    LOGV("setMode() reset Playback Path to RCV");
                    TRACE_DRIVER_IN(DRV_MIXER_SEL)
                    mixer_ctl_select(ctl, "RCV");
                    TRACE_DRIVER_OUT
                }
            }
            LOGV("setMode() closePcmOut_l()");
            closeMixer_l();
            closePcmOut_l();

            if (spOut != 0) {
                LOGV("setMode() off call force output standby");
                spOut->doStandby_l();
            }
            if (spIn != 0) {
                LOGV("setMode() off call force input standby");
                spIn->doStandby_l();
            }

            mInCallAudioMode = false;
        }

        if (mMode == AudioSystem::MODE_NORMAL) {
            if(mActivatedCP)
                mActivatedCP = false;
        }
    }

    if (spIn != 0) {
        spIn->unlock();
    }
    if (spOut != 0) {
        spOut->unlock();
    }

    return status;
}

status_t AudioHardware::setMicMute(bool state)
{
    LOGV("setMicMute(%d) mMicMute %d", state, mMicMute);
    sp<AudioStreamInALSA> spIn;
    {
        AutoMutex lock(mLock);
        if (mMicMute != state) {
            mMicMute = state;
            // in call mute is handled by RIL
            if (mMode != AudioSystem::MODE_IN_CALL) {
                spIn = getActiveInput_l();
            }
        }
    }

    if (spIn != 0) {
        spIn->standby();
    }

    return NO_ERROR;
}

status_t AudioHardware::getMicMute(bool* state)
{
    *state = mMicMute;
    return NO_ERROR;
}

status_t AudioHardware::setParameters(const String8& keyValuePairs)
{
    AudioParameter param = AudioParameter(keyValuePairs);
    String8 value;
    String8 key;
    const char BT_NREC_KEY[] = "bt_headset_nrec";
    const char BT_NREC_VALUE_ON[] = "on";

    key = String8(BT_NREC_KEY);
    if (param.get(key, value) == NO_ERROR) {
        if (value == BT_NREC_VALUE_ON) {
            mBluetoothNrec = true;
        } else {
            mBluetoothNrec = false;
            LOGD("Turning noise reduction and echo cancellation off for BT "
                 "headset");
        }
    }

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
    if (format != AudioSystem::PCM_16_BIT) {
        LOGW("getInputBufferSize bad format: %d", format);
        return 0;
    }
    if (channelCount < 1 || channelCount > 2) {
        LOGW("getInputBufferSize bad channel count: %d", channelCount);
        return 0;
    }
    if (sampleRate != 8000 && sampleRate != 11025 && sampleRate != 16000 &&
            sampleRate != 22050 && sampleRate != 44100) {
        LOGW("getInputBufferSize bad sample rate: %d", sampleRate);
        return 0;
    }

    return AudioStreamInALSA::getBufferSize(sampleRate, channelCount);
}


status_t AudioHardware::setVoiceVolume(float volume)
{
    LOGD("### setVoiceVolume");

    AutoMutex lock(mLock);
    if ( (AudioSystem::MODE_IN_CALL == mMode) && (mSecRilLibHandle) &&
         (connectRILDIfRequired() == OK) ) {

        uint32_t device = AudioSystem::DEVICE_OUT_EARPIECE;
        if (mOutput != 0) {
            device = mOutput->device();
        }
        int int_volume = (int)(volume * 5);
        SoundType type;

        LOGD("### route(%d) call volume(%f)", device, volume);
        switch (device) {
            case AudioSystem::DEVICE_OUT_EARPIECE:
                LOGD("### earpiece call volume");
                type = SOUND_TYPE_VOICE;
                break;

            case AudioSystem::DEVICE_OUT_SPEAKER:
                LOGD("### speaker call volume");
                type = SOUND_TYPE_SPEAKER;
                break;

            case AudioSystem::DEVICE_OUT_BLUETOOTH_SCO:
            case AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_HEADSET:
            case AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_CARKIT:
                LOGD("### bluetooth call volume");
                type = SOUND_TYPE_BTVOICE;
                break;

            case AudioSystem::DEVICE_OUT_WIRED_HEADSET:
            case AudioSystem::DEVICE_OUT_WIRED_HEADPHONE: // Use receive path with 3 pole headset.
                LOGD("### headset call volume");
                type = SOUND_TYPE_HEADSET;
                break;

            default:
                LOGW("### Call volume setting error!!!0x%08x \n", device);
                type = SOUND_TYPE_VOICE;
                break;
        }
        setCallVolume(mRilClient, type, int_volume);
    }

    return NO_ERROR;
}

status_t AudioHardware::setMasterVolume(float volume)
{
    LOGV("Set master volume to %f.\n", volume);
    // We return an error code here to let the audioflinger do in-software
    // volume on top of the maximum volume that we set through the SND API.
    // return error - software mixer will handle it
    return -1;
}

static const int kDumpLockRetries = 50;
static const int kDumpLockSleep = 20000;

static bool tryLock(Mutex& mutex)
{
    bool locked = false;
    for (int i = 0; i < kDumpLockRetries; ++i) {
        if (mutex.tryLock() == NO_ERROR) {
            locked = true;
            break;
        }
        usleep(kDumpLockSleep);
    }
    return locked;
}

status_t AudioHardware::dump(int fd, const Vector<String16>& args)
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;

    bool locked = tryLock(mLock);
    if (!locked) {
        snprintf(buffer, SIZE, "\n\tAudioHardware maybe deadlocked\n");
    } else {
        mLock.unlock();
    }

    snprintf(buffer, SIZE, "\tInit %s\n", (mInit) ? "OK" : "Failed");
    result.append(buffer);
    snprintf(buffer, SIZE, "\tMic Mute %s\n", (mMicMute) ? "ON" : "OFF");
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmPcm: %p\n", mPcm);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmPcmOpenCnt: %d\n", mPcmOpenCnt);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmMixer: %p\n", mMixer);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmMixerOpenCnt: %d\n", mMixerOpenCnt);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tIn Call Audio Mode %s\n",
             (mInCallAudioMode) ? "ON" : "OFF");
    result.append(buffer);
    snprintf(buffer, SIZE, "\tInput source %s\n", mInputSource.string());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmSecRilLibHandle: %p\n", mSecRilLibHandle);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmRilClient: %p\n", mRilClient);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tCP %s\n",
             (mActivatedCP) ? "Activated" : "Deactivated");
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmDriverOp: %d\n", mDriverOp);
    result.append(buffer);

    snprintf(buffer, SIZE, "\n\tmOutput %p dump:\n", mOutput.get());
    result.append(buffer);
    write(fd, result.string(), result.size());
    if (mOutput != 0) {
        mOutput->dump(fd, args);
    }

    snprintf(buffer, SIZE, "\n\t%d inputs opened:\n", mInputs.size());
    write(fd, buffer, strlen(buffer));
    for (size_t i = 0; i < mInputs.size(); i++) {
        snprintf(buffer, SIZE, "\t- input %d dump:\n", i);
        write(fd, buffer, strlen(buffer));
        mInputs[i]->dump(fd, args);
    }

    return NO_ERROR;
}

status_t AudioHardware::setIncallPath_l(uint32_t device)
{
    LOGV("setIncallPath_l: device %x", device);

    // Setup sound path for CP clocking
    if ((mSecRilLibHandle) &&
        (connectRILDIfRequired() == OK)) {

        if (mMode == AudioSystem::MODE_IN_CALL) {
            LOGD("### incall mode route (%d)", device);
            AudioPath path;
            switch(device){
                case AudioSystem::DEVICE_OUT_EARPIECE:
                    LOGD("### incall mode earpiece route");
                    path = SOUND_AUDIO_PATH_HANDSET;
                    break;

                case AudioSystem::DEVICE_OUT_SPEAKER:
                    LOGD("### incall mode speaker route");
                    path = SOUND_AUDIO_PATH_SPEAKER;
                    break;

                case AudioSystem::DEVICE_OUT_BLUETOOTH_SCO:
                case AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_HEADSET:
                case AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_CARKIT:
                    LOGD("### incall mode bluetooth route %s NR", mBluetoothNrec ? "" : "NO");
                    if (mBluetoothNrec) {
                        path = SOUND_AUDIO_PATH_BLUETOOTH;
                    } else {
                        path = SOUND_AUDIO_PATH_BLUETOOTH_NO_NR;
                    }
                    break;

                case AudioSystem::DEVICE_OUT_WIRED_HEADPHONE :
                    LOGD("### incall mode headphone route");
                    path = SOUND_AUDIO_PATH_HEADPHONE;
                    break;

                case AudioSystem::DEVICE_OUT_WIRED_HEADSET :
                    LOGD("### incall mode headset route");
                    path = SOUND_AUDIO_PATH_HEADSET;
                    break;

                default:
                    LOGW("### incall mode Error!! route = [%d]", device);
                    path = SOUND_AUDIO_PATH_HANDSET;
                    break;
            }

            setCallAudioPath(mRilClient, path);

            if (mMixer != NULL) {
                TRACE_DRIVER_IN(DRV_MIXER_GET)
                struct mixer_ctl *ctl= mixer_get_control(mMixer, "Voice Call Path", 0);
                TRACE_DRIVER_OUT
                LOGE_IF(ctl == NULL, "setIncallPath_l() could not get mixer ctl");
                if (ctl != NULL) {
                    LOGV("setIncallPath_l() Voice Call Path, (%x)", device);
                    TRACE_DRIVER_IN(DRV_MIXER_SEL)
                    mixer_ctl_select(ctl, getVoiceRouteFromDevice(device));
                    TRACE_DRIVER_OUT
                }
            }
        }
    }
    return NO_ERROR;
}

struct pcm *AudioHardware::openPcmOut_l()
{
    LOGD("openPcmOut_l() mPcmOpenCnt: %d", mPcmOpenCnt);
    if (mPcmOpenCnt++ == 0) {
        if (mPcm != NULL) {
            LOGE("openPcmOut_l() mPcmOpenCnt == 0 and mPcm == %p\n", mPcm);
            mPcmOpenCnt--;
            return NULL;
        }
        unsigned flags = PCM_OUT;

        flags |= (AUDIO_HW_OUT_PERIOD_MULT - 1) << PCM_PERIOD_SZ_SHIFT;
        flags |= (AUDIO_HW_OUT_PERIOD_CNT - PCM_PERIOD_CNT_MIN) << PCM_PERIOD_CNT_SHIFT;

        TRACE_DRIVER_IN(DRV_PCM_OPEN)
        mPcm = pcm_open(flags);
        TRACE_DRIVER_OUT
        if (!pcm_ready(mPcm)) {
            LOGE("openPcmOut_l() cannot open pcm_out driver: %s\n", pcm_error(mPcm));
            TRACE_DRIVER_IN(DRV_PCM_CLOSE)
            pcm_close(mPcm);
            TRACE_DRIVER_OUT
            mPcmOpenCnt--;
            mPcm = NULL;
        }
    }
    return mPcm;
}

void AudioHardware::closePcmOut_l()
{
    LOGD("closePcmOut_l() mPcmOpenCnt: %d", mPcmOpenCnt);
    if (mPcmOpenCnt == 0) {
        LOGE("closePcmOut_l() mPcmOpenCnt == 0");
        return;
    }

    if (--mPcmOpenCnt == 0) {
        TRACE_DRIVER_IN(DRV_PCM_CLOSE)
        pcm_close(mPcm);
        TRACE_DRIVER_OUT
        mPcm = NULL;
    }
}

struct mixer *AudioHardware::openMixer_l()
{
    LOGV("openMixer_l() mMixerOpenCnt: %d", mMixerOpenCnt);
    if (mMixerOpenCnt++ == 0) {
        if (mMixer != NULL) {
            LOGE("openMixer_l() mMixerOpenCnt == 0 and mMixer == %p\n", mMixer);
            mMixerOpenCnt--;
            return NULL;
        }
        TRACE_DRIVER_IN(DRV_MIXER_OPEN)
        mMixer = mixer_open();
        TRACE_DRIVER_OUT
        if (mMixer == NULL) {
            LOGE("openMixer_l() cannot open mixer");
            mMixerOpenCnt--;
            return NULL;
        }
    }
    return mMixer;
}

void AudioHardware::closeMixer_l()
{
    LOGV("closeMixer_l() mMixerOpenCnt: %d", mMixerOpenCnt);
    if (mMixerOpenCnt == 0) {
        LOGE("closeMixer_l() mMixerOpenCnt == 0");
        return;
    }

    if (--mMixerOpenCnt == 0) {
        TRACE_DRIVER_IN(DRV_MIXER_CLOSE)
        mixer_close(mMixer);
        TRACE_DRIVER_OUT
        mMixer = NULL;
    }
}

const char *AudioHardware::getOutputRouteFromDevice(uint32_t device)
{
    switch (device) {
    case AudioSystem::DEVICE_OUT_EARPIECE:
        return "RCV";
    case AudioSystem::DEVICE_OUT_SPEAKER:
        if (mMode == AudioSystem::MODE_RINGTONE) return "RING_SPK";
        else return "SPK";
    case AudioSystem::DEVICE_OUT_WIRED_HEADPHONE:
        if (mMode == AudioSystem::MODE_RINGTONE) return "RING_NO_MIC";
        else return "HP_NO_MIC";
    case AudioSystem::DEVICE_OUT_WIRED_HEADSET:
        if (mMode == AudioSystem::MODE_RINGTONE) return "RING_HP";
        else return "HP";
    case (AudioSystem::DEVICE_OUT_SPEAKER|AudioSystem::DEVICE_OUT_WIRED_HEADPHONE):
    case (AudioSystem::DEVICE_OUT_SPEAKER|AudioSystem::DEVICE_OUT_WIRED_HEADSET):
        if (mMode == AudioSystem::MODE_RINGTONE) return "RING_SPK_HP";
        else return "SPK_HP";
    case AudioSystem::DEVICE_OUT_BLUETOOTH_SCO:
    case AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_HEADSET:
    case AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_CARKIT:
        return "BT";
    default:
        return "OFF";
    }
}

const char *AudioHardware::getVoiceRouteFromDevice(uint32_t device)
{
    switch (device) {
    case AudioSystem::DEVICE_OUT_EARPIECE:
        return "RCV";
    case AudioSystem::DEVICE_OUT_SPEAKER:
        return "SPK";
    case AudioSystem::DEVICE_OUT_WIRED_HEADPHONE:
        return "HP_NO_MIC";
    case AudioSystem::DEVICE_OUT_WIRED_HEADSET:
        return "HP";
    case AudioSystem::DEVICE_OUT_BLUETOOTH_SCO:
    case AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_HEADSET:
    case AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_CARKIT:
        return "BT";
    default:
        return "OFF";
    }
}

const char *AudioHardware::getInputRouteFromDevice(uint32_t device)
{
    if (mMicMute) {
        return "MIC OFF";
    }

    switch (device) {
    case AudioSystem::DEVICE_IN_BUILTIN_MIC:
        return "Main Mic";
    case AudioSystem::DEVICE_IN_WIRED_HEADSET:
        return "Hands Free Mic";
    case AudioSystem::DEVICE_IN_BLUETOOTH_SCO_HEADSET:
        return "BT Sco Mic";
    default:
        return "MIC OFF";
    }
}

uint32_t AudioHardware::getInputSampleRate(uint32_t sampleRate)
{
    uint32_t i;
    uint32_t prevDelta;
    uint32_t delta;

    for (i = 0, prevDelta = 0xFFFFFFFF; i < sizeof(inputSamplingRates)/sizeof(uint32_t); i++, prevDelta = delta) {
        delta = abs(sampleRate - inputSamplingRates[i]);
        if (delta > prevDelta) break;
    }
    // i is always > 0 here
    return inputSamplingRates[i-1];
}

// getActiveInput_l() must be called with mLock held
sp <AudioHardware::AudioStreamInALSA> AudioHardware::getActiveInput_l()
{
    sp< AudioHardware::AudioStreamInALSA> spIn;

    for (size_t i = 0; i < mInputs.size(); i++) {
        // return first input found not being in standby mode
        // as only one input can be in this state
        if (!mInputs[i]->checkStandby()) {
            spIn = mInputs[i];
            break;
        }
    }

    return spIn;
}

status_t AudioHardware::setInputSource_l(String8 source)
{
     LOGV("setInputSource_l(%s)", source.string());
     if (source != mInputSource) {
         if ((source == "Default") || (mMode != AudioSystem::MODE_IN_CALL)) {
             if (mMixer) {
                 TRACE_DRIVER_IN(DRV_MIXER_GET)
                 struct mixer_ctl *ctl= mixer_get_control(mMixer, "Input Source", 0);
                 TRACE_DRIVER_OUT
                 if (ctl == NULL) {
                     return NO_INIT;
                 }
                 LOGV("mixer_ctl_select, Input Source, (%s)", source.string());
                 TRACE_DRIVER_IN(DRV_MIXER_SEL)
                 mixer_ctl_select(ctl, source.string());
                 TRACE_DRIVER_OUT
             }
         }
         mInputSource = source;
     }

     return NO_ERROR;
}


//------------------------------------------------------------------------------
//  AudioStreamOutALSA
//------------------------------------------------------------------------------

AudioHardware::AudioStreamOutALSA::AudioStreamOutALSA() :
    mHardware(0), mPcm(0), mMixer(0), mRouteCtl(0),
    mStandby(true), mDevices(0), mChannels(AUDIO_HW_OUT_CHANNELS),
    mSampleRate(AUDIO_HW_OUT_SAMPLERATE), mBufferSize(AUDIO_HW_OUT_PERIOD_BYTES),
    mDriverOp(DRV_NONE), mStandbyCnt(0)
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
    mBufferSize = AUDIO_HW_OUT_PERIOD_BYTES;

    return NO_ERROR;
}

AudioHardware::AudioStreamOutALSA::~AudioStreamOutALSA()
{
    standby();
}

ssize_t AudioHardware::AudioStreamOutALSA::write(const void* buffer, size_t bytes)
{
    //    LOGV("AudioStreamOutALSA::write(%p, %u)", buffer, bytes);
    status_t status = NO_INIT;
    const uint8_t* p = static_cast<const uint8_t*>(buffer);
    int ret;

    if (mHardware == NULL) return NO_INIT;

    { // scope for the lock

        AutoMutex lock(mLock);

        if (mStandby) {
            AutoMutex hwLock(mHardware->lock());

            LOGD("AudioHardware pcm playback is exiting standby.");
            acquire_wake_lock (PARTIAL_WAKE_LOCK, "AudioOutLock");

            sp<AudioStreamInALSA> spIn = mHardware->getActiveInput_l();
            while (spIn != 0) {
                int cnt = spIn->standbyCnt();
                mHardware->lock().unlock();
                // Mutex acquisition order is always out -> in -> hw
                spIn->lock();
                mHardware->lock().lock();
                // make sure that another thread did not change input state
                // while the mutex is released
                if ((spIn == mHardware->getActiveInput_l()) &&
                        (cnt == spIn->standbyCnt())) {
                    LOGV("AudioStreamOutALSA::write() force input standby");
                    spIn->close_l();
                    break;
                }
                spIn->unlock();
                spIn = mHardware->getActiveInput_l();
            }
            // spIn is not 0 here only if the input was active and has been
            // closed above

            // open output before input
            open_l();

            if (spIn != 0) {
                if (spIn->open_l() != NO_ERROR) {
                    spIn->doStandby_l();
                }
                spIn->unlock();
            }
            if (mPcm == NULL) {
                release_wake_lock("AudioOutLock");
                goto Error;
            }
            mStandby = false;
        }

        TRACE_DRIVER_IN(DRV_PCM_WRITE)
        ret = pcm_write(mPcm,(void*) p, bytes);
        TRACE_DRIVER_OUT

        if (ret == 0) {
            return bytes;
        }
        LOGW("write error: %d", errno);
        status = -errno;
    }
Error:

    standby();

    // Simulate audio output timing in case of error
    usleep((((bytes * 1000) / frameSize()) * 1000) / sampleRate());

    return status;
}

status_t AudioHardware::AudioStreamOutALSA::standby()
{
    if (mHardware == NULL) return NO_INIT;

    AutoMutex lock(mLock);

    { // scope for the AudioHardware lock
        AutoMutex hwLock(mHardware->lock());

        doStandby_l();
    }

    return NO_ERROR;
}

void AudioHardware::AudioStreamOutALSA::doStandby_l()
{
    mStandbyCnt++;

    if (!mStandby) {
        LOGD("AudioHardware pcm playback is going to standby.");
        release_wake_lock("AudioOutLock");
        mStandby = true;
    }

    close_l();
}

void AudioHardware::AudioStreamOutALSA::close_l()
{
    if (mMixer) {
        mHardware->closeMixer_l();
        mMixer = NULL;
        mRouteCtl = NULL;
    }
    if (mPcm) {
        mHardware->closePcmOut_l();
        mPcm = NULL;
    }
}

status_t AudioHardware::AudioStreamOutALSA::open_l()
{
    LOGV("open pcm_out driver");
    mPcm = mHardware->openPcmOut_l();
    if (mPcm == NULL) {
        return NO_INIT;
    }

    mMixer = mHardware->openMixer_l();
    if (mMixer) {
        LOGV("open playback normal");
        TRACE_DRIVER_IN(DRV_MIXER_GET)
        mRouteCtl = mixer_get_control(mMixer, "Playback Path", 0);
        TRACE_DRIVER_OUT
    }
    if (mHardware->mode() != AudioSystem::MODE_IN_CALL) {
        const char *route = mHardware->getOutputRouteFromDevice(mDevices);
        LOGV("write() wakeup setting route %s", route);
        if (mRouteCtl) {
            TRACE_DRIVER_IN(DRV_MIXER_SEL)
            mixer_ctl_select(mRouteCtl, route);
            TRACE_DRIVER_OUT
        }
    }
    return NO_ERROR;
}

status_t AudioHardware::AudioStreamOutALSA::dump(int fd, const Vector<String16>& args)
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;

    bool locked = tryLock(mLock);
    if (!locked) {
        snprintf(buffer, SIZE, "\n\t\tAudioStreamOutALSA maybe deadlocked\n");
    } else {
        mLock.unlock();
    }

    snprintf(buffer, SIZE, "\t\tmHardware: %p\n", mHardware);
    result.append(buffer);
    snprintf(buffer, SIZE, "\t\tmPcm: %p\n", mPcm);
    result.append(buffer);
    snprintf(buffer, SIZE, "\t\tmMixer: %p\n", mMixer);
    result.append(buffer);
    snprintf(buffer, SIZE, "\t\tmRouteCtl: %p\n", mRouteCtl);
    result.append(buffer);
    snprintf(buffer, SIZE, "\t\tStandby %s\n", (mStandby) ? "ON" : "OFF");
    result.append(buffer);
    snprintf(buffer, SIZE, "\t\tmDevices: 0x%08x\n", mDevices);
    result.append(buffer);
    snprintf(buffer, SIZE, "\t\tmChannels: 0x%08x\n", mChannels);
    result.append(buffer);
    snprintf(buffer, SIZE, "\t\tmSampleRate: %d\n", mSampleRate);
    result.append(buffer);
    snprintf(buffer, SIZE, "\t\tmBufferSize: %d\n", mBufferSize);
    result.append(buffer);
    snprintf(buffer, SIZE, "\t\tmDriverOp: %d\n", mDriverOp);
    result.append(buffer);

    ::write(fd, result.string(), result.size());

    return NO_ERROR;
}

bool AudioHardware::AudioStreamOutALSA::checkStandby()
{
    return mStandby;
}

status_t AudioHardware::AudioStreamOutALSA::setParameters(const String8& keyValuePairs)
{
    AudioParameter param = AudioParameter(keyValuePairs);
    status_t status = NO_ERROR;
    int device;
    LOGD("AudioStreamOutALSA::setParameters() %s", keyValuePairs.string());

    if (mHardware == NULL) return NO_INIT;

    {
        AutoMutex lock(mLock);

        if (param.getInt(String8(AudioParameter::keyRouting), device) == NO_ERROR)
        {
            AutoMutex hwLock(mHardware->lock());

            if (mDevices != (uint32_t)device) {
                mDevices = (uint32_t)device;
                if (mHardware->mode() != AudioSystem::MODE_IN_CALL) {
                    doStandby_l();
                }
            }
            if (mHardware->mode() == AudioSystem::MODE_IN_CALL) {
                mHardware->setIncallPath_l(device);
            }
            param.remove(String8(AudioParameter::keyRouting));
        }
    }

    if (param.size()) {
        status = BAD_VALUE;
    }


    return status;

}

String8 AudioHardware::AudioStreamOutALSA::getParameters(const String8& keys)
{
    AudioParameter param = AudioParameter(keys);
    String8 value;
    String8 key = String8(AudioParameter::keyRouting);

    if (param.get(key, value) == NO_ERROR) {
        param.addInt(key, (int)mDevices);
    }

    LOGV("AudioStreamOutALSA::getParameters() %s", param.toString().string());
    return param.toString();
}

status_t AudioHardware::AudioStreamOutALSA::getRenderPosition(uint32_t *dspFrames)
{
    //TODO
    return INVALID_OPERATION;
}

//------------------------------------------------------------------------------
//  AudioStreamInALSA
//------------------------------------------------------------------------------

AudioHardware::AudioStreamInALSA::AudioStreamInALSA() :
    mHardware(0), mPcm(0), mMixer(0), mRouteCtl(0),
    mStandby(true), mDevices(0), mChannels(AUDIO_HW_IN_CHANNELS), mChannelCount(1),
    mSampleRate(AUDIO_HW_IN_SAMPLERATE), mBufferSize(AUDIO_HW_IN_PERIOD_BYTES),
    mDownSampler(NULL), mReadStatus(NO_ERROR), mDriverOp(DRV_NONE),
    mStandbyCnt(0)
{
}

status_t AudioHardware::AudioStreamInALSA::set(
    AudioHardware* hw, uint32_t devices, int *pFormat,
    uint32_t *pChannels, uint32_t *pRate, AudioSystem::audio_in_acoustics acoustics)
{
    if (pFormat == 0 || *pFormat != AUDIO_HW_IN_FORMAT) {
        *pFormat = AUDIO_HW_IN_FORMAT;
        return BAD_VALUE;
    }
    if (pRate == 0) {
        return BAD_VALUE;
    }
    uint32_t rate = AudioHardware::getInputSampleRate(*pRate);
    if (rate != *pRate) {
        *pRate = rate;
        return BAD_VALUE;
    }

    if (pChannels == 0 || (*pChannels != AudioSystem::CHANNEL_IN_MONO &&
        *pChannels != AudioSystem::CHANNEL_IN_STEREO)) {
        *pChannels = AUDIO_HW_IN_CHANNELS;
        return BAD_VALUE;
    }

    mHardware = hw;

    LOGV("AudioStreamInALSA::set(%d, %d, %u)", *pFormat, *pChannels, *pRate);

    mBufferSize = getBufferSize(*pRate, AudioSystem::popCount(*pChannels));
    mDevices = devices;
    mChannels = *pChannels;
    mChannelCount = AudioSystem::popCount(mChannels);
    mSampleRate = rate;
    if (mSampleRate != AUDIO_HW_OUT_SAMPLERATE) {
        mDownSampler = new AudioHardware::DownSampler(mSampleRate,
                                                  mChannelCount,
                                                  AUDIO_HW_IN_PERIOD_SZ,
                                                  this);
        status_t status = mDownSampler->initCheck();
        if (status != NO_ERROR) {
            delete mDownSampler;
            LOGW("AudioStreamInALSA::set() downsampler init failed: %d", status);
            return status;
        }

        mPcmIn = new int16_t[AUDIO_HW_IN_PERIOD_SZ * mChannelCount];
    }
    return NO_ERROR;
}

AudioHardware::AudioStreamInALSA::~AudioStreamInALSA()
{
    standby();
    if (mDownSampler != NULL) {
        delete mDownSampler;
        if (mPcmIn != NULL) {
            delete[] mPcmIn;
        }
    }
}

ssize_t AudioHardware::AudioStreamInALSA::read(void* buffer, ssize_t bytes)
{
    //    LOGV("AudioStreamInALSA::read(%p, %u)", buffer, bytes);
    status_t status = NO_INIT;
    int ret;

    if (mHardware == NULL) return NO_INIT;

    { // scope for the lock
        AutoMutex lock(mLock);

        if (mStandby) {
            AutoMutex hwLock(mHardware->lock());

            LOGD("AudioHardware pcm capture is exiting standby.");
            acquire_wake_lock (PARTIAL_WAKE_LOCK, "AudioInLock");

            sp<AudioStreamOutALSA> spOut = mHardware->output();
            while (spOut != 0) {
                if (!spOut->checkStandby()) {
                    int cnt = spOut->standbyCnt();
                    mHardware->lock().unlock();
                    mLock.unlock();
                    // Mutex acquisition order is always out -> in -> hw
                    spOut->lock();
                    mLock.lock();
                    mHardware->lock().lock();
                    // make sure that another thread did not change output state
                    // while the mutex is released
                    if ((spOut == mHardware->output()) && (cnt == spOut->standbyCnt())) {
                        LOGV("AudioStreamInALSA::read() force output standby");
                        spOut->close_l();
                        break;
                    }
                    spOut->unlock();
                    spOut = mHardware->output();
                } else {
                    spOut.clear();
                }
            }
            // spOut is not 0 here only if the output was active and has been
            // closed above

            // open output before input
            if (spOut != 0) {
                if (spOut->open_l() != NO_ERROR) {
                    spOut->doStandby_l();
                }
                spOut->unlock();
            }

            open_l();

            if (mPcm == NULL) {
                release_wake_lock("AudioInLock");
                goto Error;
            }
            mStandby = false;
        }


        if (mDownSampler != NULL) {
            size_t frames = bytes / frameSize();
            size_t framesIn = 0;
            mReadStatus = 0;
            do {
                size_t outframes = frames - framesIn;
                mDownSampler->resample(
                        (int16_t *)buffer + (framesIn * mChannelCount),
                        &outframes);
                framesIn += outframes;
            } while ((framesIn < frames) && mReadStatus == 0);
            ret = mReadStatus;
            bytes = framesIn * frameSize();
        } else {
            TRACE_DRIVER_IN(DRV_PCM_READ)
            ret = pcm_read(mPcm, buffer, bytes);
            TRACE_DRIVER_OUT
        }

        if (ret == 0) {
            return bytes;
        }

        LOGW("read error: %d", ret);
        status = ret;
    }

Error:

    standby();

    // Simulate audio output timing in case of error
    usleep((((bytes * 1000) / frameSize()) * 1000) / sampleRate());

    return status;
}

status_t AudioHardware::AudioStreamInALSA::standby()
{
    if (mHardware == NULL) return NO_INIT;

    AutoMutex lock(mLock);

    { // scope for AudioHardware lock
        AutoMutex hwLock(mHardware->lock());

        doStandby_l();
    }
    return NO_ERROR;
}

void AudioHardware::AudioStreamInALSA::doStandby_l()
{
    mStandbyCnt++;

    if (!mStandby) {
        LOGD("AudioHardware pcm capture is going to standby.");
        release_wake_lock("AudioInLock");
        mStandby = true;
    }
    close_l();
}

void AudioHardware::AudioStreamInALSA::close_l()
{
    if (mMixer) {
        mHardware->closeMixer_l();
        mMixer = NULL;
        mRouteCtl = NULL;
    }

    if (mPcm) {
        TRACE_DRIVER_IN(DRV_PCM_CLOSE)
        pcm_close(mPcm);
        TRACE_DRIVER_OUT
        mPcm = NULL;
    }
}

status_t AudioHardware::AudioStreamInALSA::open_l()
{
    unsigned flags = PCM_IN;
    if (mChannels == AudioSystem::CHANNEL_IN_MONO) {
        flags |= PCM_MONO;
    }
    flags |= (AUDIO_HW_IN_PERIOD_MULT - 1) << PCM_PERIOD_SZ_SHIFT;
    flags |= (AUDIO_HW_IN_PERIOD_CNT - PCM_PERIOD_CNT_MIN)
            << PCM_PERIOD_CNT_SHIFT;

    LOGV("open pcm_in driver");
    TRACE_DRIVER_IN(DRV_PCM_OPEN)
    mPcm = pcm_open(flags);
    TRACE_DRIVER_OUT
    if (!pcm_ready(mPcm)) {
        LOGE("cannot open pcm_in driver: %s\n", pcm_error(mPcm));
        TRACE_DRIVER_IN(DRV_PCM_CLOSE)
        pcm_close(mPcm);
        TRACE_DRIVER_OUT
        mPcm = NULL;
        return NO_INIT;
    }

    if (mDownSampler != NULL) {
        mInPcmInBuf = 0;
        mDownSampler->reset();
    }

    mMixer = mHardware->openMixer_l();
    if (mMixer) {
        TRACE_DRIVER_IN(DRV_MIXER_GET)
        mRouteCtl = mixer_get_control(mMixer, "Capture MIC Path", 0);
        TRACE_DRIVER_OUT
    }

    if (mHardware->mode() != AudioSystem::MODE_IN_CALL) {
        const char *route = mHardware->getInputRouteFromDevice(mDevices);
        LOGV("read() wakeup setting route %s", route);
        if (mRouteCtl) {
            TRACE_DRIVER_IN(DRV_MIXER_SEL)
            mixer_ctl_select(mRouteCtl, route);
            TRACE_DRIVER_OUT
        }
    }

    return NO_ERROR;
}

status_t AudioHardware::AudioStreamInALSA::dump(int fd, const Vector<String16>& args)
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;

    bool locked = tryLock(mLock);
    if (!locked) {
        snprintf(buffer, SIZE, "\n\t\tAudioStreamInALSA maybe deadlocked\n");
    } else {
        mLock.unlock();
    }

    snprintf(buffer, SIZE, "\t\tmHardware: %p\n", mHardware);
    result.append(buffer);
    snprintf(buffer, SIZE, "\t\tmPcm: %p\n", mPcm);
    result.append(buffer);
    snprintf(buffer, SIZE, "\t\tmMixer: %p\n", mMixer);
    result.append(buffer);
    snprintf(buffer, SIZE, "\t\tStandby %s\n", (mStandby) ? "ON" : "OFF");
    result.append(buffer);
    snprintf(buffer, SIZE, "\t\tmDevices: 0x%08x\n", mDevices);
    result.append(buffer);
    snprintf(buffer, SIZE, "\t\tmChannels: 0x%08x\n", mChannels);
    result.append(buffer);
    snprintf(buffer, SIZE, "\t\tmSampleRate: %d\n", mSampleRate);
    result.append(buffer);
    snprintf(buffer, SIZE, "\t\tmBufferSize: %d\n", mBufferSize);
    result.append(buffer);
    snprintf(buffer, SIZE, "\t\tmDriverOp: %d\n", mDriverOp);
    result.append(buffer);
    write(fd, result.string(), result.size());

    return NO_ERROR;
}

bool AudioHardware::AudioStreamInALSA::checkStandby()
{
    return mStandby;
}

status_t AudioHardware::AudioStreamInALSA::setParameters(const String8& keyValuePairs)
{
    AudioParameter param = AudioParameter(keyValuePairs);
    status_t status = NO_ERROR;
    int value;
    String8 source;

    LOGD("AudioStreamInALSA::setParameters() %s", keyValuePairs.string());

    if (mHardware == NULL) return NO_INIT;

    {
        AutoMutex lock(mLock);

        if (param.get(String8(INPUT_SOURCE_KEY), source) == NO_ERROR) {
            AutoMutex hwLock(mHardware->lock());

            mHardware->openMixer_l();
            mHardware->setInputSource_l(source);
            mHardware->closeMixer_l();

            param.remove(String8(INPUT_SOURCE_KEY));
        }

        if (param.getInt(String8(AudioParameter::keyRouting), value) == NO_ERROR)
        {
            if (value != 0) {
                AutoMutex hwLock(mHardware->lock());

                if (mDevices != (uint32_t)value) {
                    mDevices = (uint32_t)value;
                    if (mHardware->mode() != AudioSystem::MODE_IN_CALL) {
                        doStandby_l();
                    }
                }
            }
            param.remove(String8(AudioParameter::keyRouting));
        }
    }


    if (param.size()) {
        status = BAD_VALUE;
    }

    return status;

}

String8 AudioHardware::AudioStreamInALSA::getParameters(const String8& keys)
{
    AudioParameter param = AudioParameter(keys);
    String8 value;
    String8 key = String8(AudioParameter::keyRouting);

    if (param.get(key, value) == NO_ERROR) {
        param.addInt(key, (int)mDevices);
    }

    LOGV("AudioStreamInALSA::getParameters() %s", param.toString().string());
    return param.toString();
}

status_t AudioHardware::AudioStreamInALSA::getNextBuffer(AudioHardware::BufferProvider::Buffer* buffer)
{
    if (mPcm == NULL) {
        buffer->raw = NULL;
        buffer->frameCount = 0;
        mReadStatus = NO_INIT;
        return NO_INIT;
    }

    if (mInPcmInBuf == 0) {
        TRACE_DRIVER_IN(DRV_PCM_READ)
        mReadStatus = pcm_read(mPcm,(void*) mPcmIn, AUDIO_HW_IN_PERIOD_SZ * frameSize());
        TRACE_DRIVER_OUT
        if (mReadStatus != 0) {
            buffer->raw = NULL;
            buffer->frameCount = 0;
            return mReadStatus;
        }
        mInPcmInBuf = AUDIO_HW_IN_PERIOD_SZ;
    }

    buffer->frameCount = (buffer->frameCount > mInPcmInBuf) ? mInPcmInBuf : buffer->frameCount;
    buffer->i16 = mPcmIn + (AUDIO_HW_IN_PERIOD_SZ - mInPcmInBuf) * mChannelCount;

    return mReadStatus;
}

void AudioHardware::AudioStreamInALSA::releaseBuffer(Buffer* buffer)
{
    mInPcmInBuf -= buffer->frameCount;
}

size_t AudioHardware::AudioStreamInALSA::getBufferSize(uint32_t sampleRate, int channelCount)
{
    size_t ratio;

    switch (sampleRate) {
    case 8000:
    case 11025:
        ratio = 4;
        break;
    case 16000:
    case 22050:
        ratio = 2;
        break;
    case 44100:
    default:
        ratio = 1;
        break;
    }

    return (AUDIO_HW_IN_PERIOD_SZ*channelCount*sizeof(int16_t)) / ratio ;
}

//------------------------------------------------------------------------------
//  DownSampler
//------------------------------------------------------------------------------

/*
 * 2.30 fixed point FIR filter coefficients for conversion 44100 -> 22050.
 * (Works equivalently for 22010 -> 11025 or any other halving, of course.)
 *
 * Transition band from about 18 kHz, passband ripple < 0.1 dB,
 * stopband ripple at about -55 dB, linear phase.
 *
 * Design and display in MATLAB or Octave using:
 *
 * filter = fir1(19, 0.5); filter = round(filter * 2**30); freqz(filter * 2**-30);
 */
static const int32_t filter_22khz_coeff[] = {
    2089257, 2898328, -5820678, -10484531,
    19038724, 30542725, -50469415, -81505260,
    152544464, 478517512, 478517512, 152544464,
    -81505260, -50469415, 30542725, 19038724,
    -10484531, -5820678, 2898328, 2089257,
};
#define NUM_COEFF_22KHZ (sizeof(filter_22khz_coeff) / sizeof(filter_22khz_coeff[0]))
#define OVERLAP_22KHZ (NUM_COEFF_22KHZ - 2)

/*
 * Convolution of signals A and reverse(B). (In our case, the filter response
 * is symmetric, so the reversing doesn't matter.)
 * A is taken to be in 0.16 fixed-point, and B is taken to be in 2.30 fixed-point.
 * The answer will be in 16.16 fixed-point, unclipped.
 *
 * This function would probably be the prime candidate for SIMD conversion if
 * you want more speed.
 */
int32_t fir_convolve(const int16_t* a, const int32_t* b, int num_samples)
{
        int32_t sum = 1 << 13;
        for (int i = 0; i < num_samples; ++i) {
                sum += a[i] * (b[i] >> 16);
        }
        return sum >> 14;
}

/* Clip from 16.16 fixed-point to 0.16 fixed-point. */
int16_t clip(int32_t x)
{
    if (x < -32768) {
        return -32768;
    } else if (x > 32767) {
        return 32767;
    } else {
        return x;
    }
}

/*
 * Convert a chunk from 44 kHz to 22 kHz. Will update num_samples_in and num_samples_out
 * accordingly, since it may leave input samples in the buffer due to overlap.
 *
 * Input and output are taken to be in 0.16 fixed-point.
 */
void resample_2_1(int16_t* input, int16_t* output, int* num_samples_in, int* num_samples_out)
{
    if (*num_samples_in < (int)NUM_COEFF_22KHZ) {
        *num_samples_out = 0;
        return;
    }

    int odd_smp = *num_samples_in & 0x1;
    int num_samples = *num_samples_in - odd_smp - OVERLAP_22KHZ;

    for (int i = 0; i < num_samples; i += 2) {
            output[i / 2] = clip(fir_convolve(input + i, filter_22khz_coeff, NUM_COEFF_22KHZ));
    }

    memmove(input, input + num_samples, (OVERLAP_22KHZ + odd_smp) * sizeof(*input));
    *num_samples_out = num_samples / 2;
    *num_samples_in = OVERLAP_22KHZ + odd_smp;
}

/*
 * 2.30 fixed point FIR filter coefficients for conversion 22050 -> 16000,
 * or 11025 -> 8000.
 *
 * Transition band from about 14 kHz, passband ripple < 0.1 dB,
 * stopband ripple at about -50 dB, linear phase.
 *
 * Design and display in MATLAB or Octave using:
 *
 * filter = fir1(23, 16000 / 22050); filter = round(filter * 2**30); freqz(filter * 2**-30);
 */
static const int32_t filter_16khz_coeff[] = {
    2057290, -2973608, 1880478, 4362037,
    -14639744, 18523609, -1609189, -38502470,
    78073125, -68353935, -59103896, 617555440,
    617555440, -59103896, -68353935, 78073125,
    -38502470, -1609189, 18523609, -14639744,
    4362037, 1880478, -2973608, 2057290,
};
#define NUM_COEFF_16KHZ (sizeof(filter_16khz_coeff) / sizeof(filter_16khz_coeff[0]))
#define OVERLAP_16KHZ (NUM_COEFF_16KHZ - 1)

/*
 * Convert a chunk from 22 kHz to 16 kHz. Will update num_samples_in and
 * num_samples_out accordingly, since it may leave input samples in the buffer
 * due to overlap.
 *
 * This implementation is rather ad-hoc; it first low-pass filters the data
 * into a temporary buffer, and then converts chunks of 441 input samples at a
 * time into 320 output samples by simple linear interpolation. A better
 * implementation would use a polyphase filter bank to do these two operations
 * in one step.
 *
 * Input and output are taken to be in 0.16 fixed-point.
 */

#define RESAMPLE_16KHZ_SAMPLES_IN 441
#define RESAMPLE_16KHZ_SAMPLES_OUT 320

void resample_441_320(int16_t* input, int16_t* output, int* num_samples_in, int* num_samples_out)
{
    const int num_blocks = (*num_samples_in - OVERLAP_16KHZ) / RESAMPLE_16KHZ_SAMPLES_IN;
    if (num_blocks < 1) {
        *num_samples_out = 0;
        return;
    }

    for (int i = 0; i < num_blocks; ++i) {
        uint32_t tmp[RESAMPLE_16KHZ_SAMPLES_IN];
        for (int j = 0; j < RESAMPLE_16KHZ_SAMPLES_IN; ++j) {
            tmp[j] = fir_convolve(input + i * RESAMPLE_16KHZ_SAMPLES_IN + j,
                          filter_16khz_coeff,
                          NUM_COEFF_16KHZ);
        }

        const float step_float = (float)RESAMPLE_16KHZ_SAMPLES_IN / (float)RESAMPLE_16KHZ_SAMPLES_OUT;

        uint32_t in_sample_num = 0;   // 16.16 fixed point
        const uint32_t step = (uint32_t)(step_float * 65536.0f + 0.5f);  // 16.16 fixed point
        for (int j = 0; j < RESAMPLE_16KHZ_SAMPLES_OUT; ++j, in_sample_num += step) {
            const uint32_t whole = in_sample_num >> 16;
            const uint32_t frac = (in_sample_num & 0xffff);  // 0.16 fixed point
            const int32_t s1 = tmp[whole];
            const int32_t s2 = tmp[whole + 1];
            *output++ = clip(s1 + (((s2 - s1) * (int32_t)frac) >> 16));
        }
    }

    const int samples_consumed = num_blocks * RESAMPLE_16KHZ_SAMPLES_IN;
    memmove(input, input + samples_consumed, (*num_samples_in - samples_consumed) * sizeof(*input));
    *num_samples_in -= samples_consumed;
    *num_samples_out = RESAMPLE_16KHZ_SAMPLES_OUT * num_blocks;
}


AudioHardware::DownSampler::DownSampler(uint32_t outSampleRate,
                                    uint32_t channelCount,
                                    uint32_t frameCount,
                                    AudioHardware::BufferProvider* provider)
    :  mStatus(NO_INIT), mProvider(provider), mSampleRate(outSampleRate),
       mChannelCount(channelCount), mFrameCount(frameCount),
       mInLeft(NULL), mInRight(NULL), mTmpLeft(NULL), mTmpRight(NULL),
       mTmp2Left(NULL), mTmp2Right(NULL), mOutLeft(NULL), mOutRight(NULL)

{
    LOGV("AudioHardware::DownSampler() cstor %p SR %d channels %d frames %d",
         this, mSampleRate, mChannelCount, mFrameCount);

    if (mSampleRate != 8000 && mSampleRate != 11025 && mSampleRate != 16000 &&
            mSampleRate != 22050) {
        LOGW("AudioHardware::DownSampler cstor: bad sampling rate: %d", mSampleRate);
        return;
    }

    mInLeft = new int16_t[mFrameCount];
    mInRight = new int16_t[mFrameCount];
    mTmpLeft = new int16_t[mFrameCount];
    mTmpRight = new int16_t[mFrameCount];
    mTmp2Left = new int16_t[mFrameCount];
    mTmp2Right = new int16_t[mFrameCount];
    mOutLeft = new int16_t[mFrameCount];
    mOutRight = new int16_t[mFrameCount];

    mStatus = NO_ERROR;
}

AudioHardware::DownSampler::~DownSampler()
{
    if (mInLeft) delete[] mInLeft;
    if (mInRight) delete[] mInRight;
    if (mTmpLeft) delete[] mTmpLeft;
    if (mTmpRight) delete[] mTmpRight;
    if (mTmp2Left) delete[] mTmp2Left;
    if (mTmp2Right) delete[] mTmp2Right;
    if (mOutLeft) delete[] mOutLeft;
    if (mOutRight) delete[] mOutRight;
}

void AudioHardware::DownSampler::reset()
{
    mInInBuf = 0;
    mInTmpBuf = 0;
    mInTmp2Buf = 0;
    mOutBufPos = 0;
    mInOutBuf = 0;
}


int AudioHardware::DownSampler::resample(int16_t* out, size_t *outFrameCount)
{
    if (mStatus != NO_ERROR) {
        return mStatus;
    }

    if (out == NULL || outFrameCount == NULL) {
        return BAD_VALUE;
    }

    int16_t *outLeft = mTmp2Left;
    int16_t *outRight = mTmp2Left;
    if (mSampleRate == 22050) {
        outLeft = mTmpLeft;
        outRight = mTmpRight;
    } else if (mSampleRate == 8000){
        outLeft = mOutLeft;
        outRight = mOutRight;
    }

    int outFrames = 0;
    int remaingFrames = *outFrameCount;

    if (mInOutBuf) {
        int frames = (remaingFrames > mInOutBuf) ? mInOutBuf : remaingFrames;

        for (int i = 0; i < frames; ++i) {
            out[i] = outLeft[mOutBufPos + i];
        }
        if (mChannelCount == 2) {
            for (int i = 0; i < frames; ++i) {
                out[i * 2] = outLeft[mOutBufPos + i];
                out[i * 2 + 1] = outRight[mOutBufPos + i];
            }
        }
        remaingFrames -= frames;
        mInOutBuf -= frames;
        mOutBufPos += frames;
        outFrames += frames;
    }

    while (remaingFrames) {
        LOGW_IF((mInOutBuf != 0), "mInOutBuf should be 0 here");

        AudioHardware::BufferProvider::Buffer buf;
        buf.frameCount =  mFrameCount - mInInBuf;
        int ret = mProvider->getNextBuffer(&buf);
        if (buf.raw == NULL) {
            *outFrameCount = outFrames;
            return ret;
        }

        for (size_t i = 0; i < buf.frameCount; ++i) {
            mInLeft[i + mInInBuf] = buf.i16[i];
        }
        if (mChannelCount == 2) {
            for (size_t i = 0; i < buf.frameCount; ++i) {
                mInLeft[i + mInInBuf] = buf.i16[i * 2];
                mInRight[i + mInInBuf] = buf.i16[i * 2 + 1];
            }
        }
        mInInBuf += buf.frameCount;
        mProvider->releaseBuffer(&buf);

        /* 44010 -> 22050 */
        {
            int samples_in_left = mInInBuf;
            int samples_out_left;
            resample_2_1(mInLeft, mTmpLeft + mInTmpBuf, &samples_in_left, &samples_out_left);

            if (mChannelCount == 2) {
                int samples_in_right = mInInBuf;
                int samples_out_right;
                resample_2_1(mInRight, mTmpRight + mInTmpBuf, &samples_in_right, &samples_out_right);
            }

            mInInBuf = samples_in_left;
            mInTmpBuf += samples_out_left;
            mInOutBuf = samples_out_left;
        }

        if (mSampleRate == 11025 || mSampleRate == 8000) {
            /* 22050 - > 11025 */
            int samples_in_left = mInTmpBuf;
            int samples_out_left;
            resample_2_1(mTmpLeft, mTmp2Left + mInTmp2Buf, &samples_in_left, &samples_out_left);

            if (mChannelCount == 2) {
                int samples_in_right = mInTmpBuf;
                int samples_out_right;
                resample_2_1(mTmpRight, mTmp2Right + mInTmp2Buf, &samples_in_right, &samples_out_right);
            }


            mInTmpBuf = samples_in_left;
            mInTmp2Buf += samples_out_left;
            mInOutBuf = samples_out_left;

            if (mSampleRate == 8000) {
                /* 11025 -> 8000*/
                int samples_in_left = mInTmp2Buf;
                int samples_out_left;
                resample_441_320(mTmp2Left, mOutLeft, &samples_in_left, &samples_out_left);

                if (mChannelCount == 2) {
                    int samples_in_right = mInTmp2Buf;
                    int samples_out_right;
                    resample_441_320(mTmp2Right, mOutRight, &samples_in_right, &samples_out_right);
                }

                mInTmp2Buf = samples_in_left;
                mInOutBuf = samples_out_left;
            } else {
                mInTmp2Buf = 0;
            }

        } else if (mSampleRate == 16000) {
            /* 22050 -> 16000*/
            int samples_in_left = mInTmpBuf;
            int samples_out_left;
            resample_441_320(mTmpLeft, mTmp2Left, &samples_in_left, &samples_out_left);

            if (mChannelCount == 2) {
                int samples_in_right = mInTmpBuf;
                int samples_out_right;
                resample_441_320(mTmpRight, mTmp2Right, &samples_in_right, &samples_out_right);
            }

            mInTmpBuf = samples_in_left;
            mInOutBuf = samples_out_left;
        } else {
            mInTmpBuf = 0;
        }

        int frames = (remaingFrames > mInOutBuf) ? mInOutBuf : remaingFrames;

        for (int i = 0; i < frames; ++i) {
            out[outFrames + i] = outLeft[i];
        }
        if (mChannelCount == 2) {
            for (int i = 0; i < frames; ++i) {
                out[(outFrames + i) * 2] = outLeft[i];
                out[(outFrames + i) * 2 + 1] = outRight[i];
            }
        }
        remaingFrames -= frames;
        outFrames += frames;
        mOutBufPos = frames;
        mInOutBuf -= frames;
    }

    return 0;
}







//------------------------------------------------------------------------------
//  Factory
//------------------------------------------------------------------------------

extern "C" AudioHardwareInterface* createAudioHardware(void) {
    return new AudioHardware();
}

}; // namespace android
