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
#include <tinyalsa/asoundlib.h>
}


namespace android_audio_legacy {

const uint32_t AudioHardware::inputConfigTable[][AudioHardware::INPUT_CONFIG_CNT] = {
        {8000, 4},
        {11025, 4},
        {16000, 2},
        {22050, 2},
        {32000, 1},
        {44100, 1}
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

const char *AudioHardware::inputPathNameDefault = "Default";
const char *AudioHardware::inputPathNameCamcorder = "Camcorder";
const char *AudioHardware::inputPathNameVoiceRecognition = "Voice Recognition";

AudioHardware::AudioHardware() :
    mInit(false),
    mMicMute(false),
    mPcm(NULL),
    mMixer(NULL),
    mPcmOpenCnt(0),
    mMixerOpenCnt(0),
    mInCallAudioMode(false),
    mVoiceVol(1.0f),
    mInputSource(AUDIO_SOURCE_DEFAULT),
    mBluetoothNrec(true),
    mTTYMode(TTY_MODE_OFF),
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

    // Mutex acquisition order is always out -> in -> hw
    AutoMutex lock(mLock);

    spOut = mOutput;
    while (spOut != 0) {
        if (!spOut->checkStandby()) {
            int cnt = spOut->prepareLock();
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
        int cnt = spIn->prepareLock();
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
            setInputSource_l(AUDIO_SOURCE_DEFAULT);
            setVoiceVolume_l(mVoiceVol);
            mInCallAudioMode = true;
        }
        if (mMode == AudioSystem::MODE_NORMAL && mInCallAudioMode) {
            setInputSource_l(mInputSource);
            if (mMixer != NULL) {
                TRACE_DRIVER_IN(DRV_MIXER_GET)
                struct mixer_ctl *ctl= mixer_get_ctl_by_name(mMixer, "Playback Path");
                TRACE_DRIVER_OUT
                if (ctl != NULL) {
                    LOGV("setMode() reset Playback Path to RCV");
                    TRACE_DRIVER_IN(DRV_MIXER_SEL)
                    mixer_ctl_set_enum_by_string(ctl, "RCV");
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
    const char TTY_MODE_KEY[] = "tty_mode";
    const char TTY_MODE_VALUE_OFF[] = "tty_off";
    const char TTY_MODE_VALUE_VCO[] = "tty_vco";
    const char TTY_MODE_VALUE_HCO[] = "tty_hco";
    const char TTY_MODE_VALUE_FULL[] = "tty_full";

    key = String8(BT_NREC_KEY);
    if (param.get(key, value) == NO_ERROR) {
        if (value == BT_NREC_VALUE_ON) {
            mBluetoothNrec = true;
        } else {
            mBluetoothNrec = false;
            LOGD("Turning noise reduction and echo cancellation off for BT "
                 "headset");
        }
        param.remove(String8(BT_NREC_KEY));
    }

    key = String8(TTY_MODE_KEY);
    if (param.get(key, value) == NO_ERROR) {
        int ttyMode;
        if (value == TTY_MODE_VALUE_OFF) {
            ttyMode = TTY_MODE_OFF;
        } else if (value == TTY_MODE_VALUE_VCO) {
            ttyMode = TTY_MODE_VCO;
        } else if (value == TTY_MODE_VALUE_HCO) {
            ttyMode = TTY_MODE_HCO;
        } else if (value == TTY_MODE_VALUE_FULL) {
            ttyMode = TTY_MODE_FULL;
        } else {
            return BAD_VALUE;
        }

        if (ttyMode != mTTYMode) {
            LOGV("new tty mode %d", ttyMode);
            mTTYMode = ttyMode;
            if (mOutput != 0 && mMode == AudioSystem::MODE_IN_CALL) {
                setIncallPath_l(mOutput->device());
            }
        }
        param.remove(String8(TTY_MODE_KEY));
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

    if (sampleRate != getInputSampleRate(sampleRate)) {
        LOGW("getInputBufferSize bad sample rate: %d", sampleRate);
        return 0;
    }

    return AudioStreamInALSA::getBufferSize(sampleRate, channelCount);
}

status_t AudioHardware::setVoiceVolume(float volume)
{
    AutoMutex lock(mLock);

    setVoiceVolume_l(volume);

    return NO_ERROR;
}

void AudioHardware::setVoiceVolume_l(float volume)
{
    LOGD("### setVoiceVolume_l");

    mVoiceVol = volume;

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
    snprintf(buffer, SIZE, "\tInput source %d\n", mInputSource);
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
                struct mixer_ctl *ctl= mixer_get_ctl_by_name(mMixer, "Voice Call Path");
                TRACE_DRIVER_OUT
                LOGE_IF(ctl == NULL, "setIncallPath_l() could not get mixer ctl");
                if (ctl != NULL) {
                    LOGV("setIncallPath_l() Voice Call Path, (%x)", device);
                    TRACE_DRIVER_IN(DRV_MIXER_SEL)
                    mixer_ctl_set_enum_by_string(ctl, getVoiceRouteFromDevice(device));
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

        struct pcm_config config = {
            channels : 2,
            rate : AUDIO_HW_OUT_SAMPLERATE,
            period_size : AUDIO_HW_OUT_PERIOD_SZ,
            period_count : AUDIO_HW_OUT_PERIOD_CNT,
            format : PCM_FORMAT_S16_LE,
        };

        TRACE_DRIVER_IN(DRV_PCM_OPEN)
        mPcm = pcm_open(0, 0, flags, &config);
        TRACE_DRIVER_OUT
        if (!pcm_is_ready(mPcm)) {
            LOGE("openPcmOut_l() cannot open pcm_out driver: %s\n", pcm_get_error(mPcm));
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
        mMixer = mixer_open(0);
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
    case AudioSystem::DEVICE_OUT_WIRED_HEADSET:
        switch (mTTYMode) {
        case TTY_MODE_VCO:
            return "TTY_VCO";
        case TTY_MODE_HCO:
            return "TTY_HCO";
        case TTY_MODE_FULL:
            return "TTY_FULL";
        case TTY_MODE_OFF:
        default:
            if (device == AudioSystem::DEVICE_OUT_WIRED_HEADPHONE) {
                return "HP_NO_MIC";
            } else {
                return "HP";
            }
        }
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
    size_t i;
    uint32_t prevDelta;
    uint32_t delta;
    size_t size = sizeof(inputConfigTable)/sizeof(uint32_t)/INPUT_CONFIG_CNT;

    for (i = 0, prevDelta = 0xFFFFFFFF; i < size; i++, prevDelta = delta) {
        delta = abs(sampleRate - inputConfigTable[i][INPUT_CONFIG_SAMPLE_RATE]);
        if (delta > prevDelta) break;
    }
    // i is always > 0 here
    return inputConfigTable[i-1][INPUT_CONFIG_SAMPLE_RATE];
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

status_t AudioHardware::setInputSource_l(audio_source source)
{
     LOGV("setInputSource_l(%d)", source);
     if (source != mInputSource) {
         if ((source == AUDIO_SOURCE_DEFAULT) || (mMode != AudioSystem::MODE_IN_CALL)) {
             if (mMixer) {
                 TRACE_DRIVER_IN(DRV_MIXER_GET)
                 struct mixer_ctl *ctl= mixer_get_ctl_by_name(mMixer, "Input Source");
                 TRACE_DRIVER_OUT
                 if (ctl == NULL) {
                     return NO_INIT;
                 }
                 const char* sourceName;
                 switch (source) {
                     case AUDIO_SOURCE_DEFAULT: // intended fall-through
                     case AUDIO_SOURCE_MIC:     // intended fall-through
                     case AUDIO_SOURCE_VOICE_COMMUNICATION:
                         sourceName = inputPathNameDefault;
                         break;
                     case AUDIO_SOURCE_CAMCORDER:
                         sourceName = inputPathNameCamcorder;
                         break;
                     case AUDIO_SOURCE_VOICE_RECOGNITION:
                         sourceName = inputPathNameVoiceRecognition;
                         break;
                     case AUDIO_SOURCE_VOICE_UPLINK:   // intended fall-through
                     case AUDIO_SOURCE_VOICE_DOWNLINK: // intended fall-through
                     case AUDIO_SOURCE_VOICE_CALL:     // intended fall-through
                     default:
                         return NO_INIT;
                 }
                 LOGV("mixer_ctl_set_enum_by_string, Input Source, (%s)", sourceName);
                 TRACE_DRIVER_IN(DRV_MIXER_SEL)
                 mixer_ctl_set_enum_by_string(ctl, sourceName);
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
    mDriverOp(DRV_NONE), mStandbyCnt(0), mSleepReq(false)
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

    if (mSleepReq) {
        // 10ms are always shorter than the time to reconfigure the audio path
        // which is the only condition when mSleepReq would be true.
        usleep(10000);
    }

    { // scope for the lock

        AutoMutex lock(mLock);

        if (mStandby) {
            AutoMutex hwLock(mHardware->lock());

            LOGD("AudioHardware pcm playback is exiting standby.");
            acquire_wake_lock (PARTIAL_WAKE_LOCK, "AudioOutLock");

            sp<AudioStreamInALSA> spIn = mHardware->getActiveInput_l();
            while (spIn != 0) {
                int cnt = spIn->prepareLock();
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

    mSleepReq = true;
    {
        AutoMutex lock(mLock);
        mSleepReq = false;

        { // scope for the AudioHardware lock
            AutoMutex hwLock(mHardware->lock());

            doStandby_l();
        }
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
        mRouteCtl = mixer_get_ctl_by_name(mMixer, "Playback Path");
        TRACE_DRIVER_OUT
    }
    if (mHardware->mode() != AudioSystem::MODE_IN_CALL) {
        const char *route = mHardware->getOutputRouteFromDevice(mDevices);
        LOGV("write() wakeup setting route %s", route);
        if (mRouteCtl) {
            TRACE_DRIVER_IN(DRV_MIXER_SEL)
            mixer_ctl_set_enum_by_string(mRouteCtl, route);
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

    mSleepReq = true;
    {
        AutoMutex lock(mLock);
        mSleepReq = false;
        if (param.getInt(String8(AudioParameter::keyRouting), device) == NO_ERROR)
        {
            if (device != 0) {
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

int AudioHardware::AudioStreamOutALSA::prepareLock()
{
    // request sleep next time write() is called so that caller can acquire
    // mLock
    mSleepReq = true;
    return mStandbyCnt;
}

void AudioHardware::AudioStreamOutALSA::lock()
{
    mLock.lock();
    mSleepReq = false;
}

void AudioHardware::AudioStreamOutALSA::unlock() {
    mLock.unlock();
}

//------------------------------------------------------------------------------
//  AudioStreamInALSA
//------------------------------------------------------------------------------

AudioHardware::AudioStreamInALSA::AudioStreamInALSA() :
    mHardware(0), mPcm(0), mMixer(0), mRouteCtl(0),
    mStandby(true), mDevices(0), mChannels(AUDIO_HW_IN_CHANNELS), mChannelCount(1),
    mSampleRate(AUDIO_HW_IN_SAMPLERATE), mBufferSize(AUDIO_HW_IN_PERIOD_BYTES),
    mDownSampler(NULL), mReadStatus(NO_ERROR), mInputBuf(NULL),
    mDriverOp(DRV_NONE), mStandbyCnt(0), mSleepReq(false),
    mProcBuf(NULL), mProcBufSize(0)
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
        mDownSampler = new AudioHardware::ReSampler(AUDIO_HW_OUT_SAMPLERATE,
                                                    mSampleRate,
                                                    mChannelCount,
                                                    this);
        status_t status = mDownSampler->initCheck();
        if (status != NO_ERROR) {
            delete mDownSampler;
            LOGW("AudioStreamInALSA::set() downsampler init failed: %d", status);
            return status;
        }
    }
    mInputBuf = new int16_t[AUDIO_HW_IN_PERIOD_SZ * mChannelCount];
    return NO_ERROR;
}

AudioHardware::AudioStreamInALSA::~AudioStreamInALSA()
{
    standby();
    delete mDownSampler;
    delete[] mInputBuf;
    delete[] mProcBuf;
}

// readFrames() reads frames from kernel driver, down samples to capture rate if necessary
// and output the number of frames requested to the buffer specified
ssize_t AudioHardware::AudioStreamInALSA::readFrames(void* buffer, ssize_t frames)
{
    ssize_t framesWr = 0;
    while (framesWr < frames) {
        size_t framesRd = frames - framesWr;
        if (mDownSampler != NULL) {
            mDownSampler->resample(
                    (int16_t *)((char *)buffer + framesWr * frameSize()),
                    &framesRd);
        } else {
            AudioHardware::BufferProvider::Buffer buf;
            buf.frameCount = framesRd;
            getNextBuffer(&buf);
            if (buf.raw != NULL) {
                memcpy((char *)buffer + framesWr * frameSize(),
                        buf.raw,
                        buf.frameCount * frameSize());
                framesRd = buf.frameCount;
            }
            releaseBuffer(&buf);
        }
        // mReadStatus is updated by getNextBuffer() also called by mDownSampler->resample()
        if (mReadStatus != 0) {
            return mReadStatus;
        }
        framesWr += framesRd;
    }
    return framesWr;
}

// processFrames() reads frames from kernel driver (via readFrames()), calls the active
// audio pre processings and output the number of frames requested to the buffer specified
ssize_t AudioHardware::AudioStreamInALSA::processFrames(void* buffer, ssize_t frames)
{
    ssize_t framesWr = 0;
    while (framesWr < frames) {
        // first reload enough frames at the end of process input buffer
        if (mProcFramesIn < (size_t)frames) {
            // expand process input buffer if necessary
            if (mProcBufSize < (size_t)frames) {
                mProcBufSize = (size_t)frames;
                mProcBuf = (int16_t *)realloc(mProcBuf,
                                              mProcBufSize * mChannelCount * sizeof(int16_t));
                LOGV("read: mProcBuf size extended to %d frames", mProcBufSize);
            }
            ssize_t framesRd = readFrames(mProcBuf + mProcFramesIn * mChannelCount,
                                          frames - mProcFramesIn);
            if (framesRd < 0) {
                framesWr = framesRd;
                break;
            }
            mProcFramesIn += framesRd;
        }

        // inBuf.frameCount and outBuf.frameCount indicate respectively the maximum number of frames
        // to be consumed and produced by process()
        audio_buffer_t inBuf = {
                mProcFramesIn,
                {mProcBuf}
        };
        audio_buffer_t outBuf = {
                frames - framesWr,
                {(int16_t *)buffer + framesWr * mChannelCount}
        };

        for (size_t i = 0; i < mPreprocessors.size(); i++) {
            (*mPreprocessors[i])->process(mPreprocessors[i],
                                                   &inBuf,
                                                   &outBuf);
        }

        // process() has updated the number of frames consumed and produced in
        // inBuf.frameCount and outBuf.frameCount respectively
        // move remaining frames to the beginning of mProcBuf
        memcpy(mProcBuf,
               mProcBuf + inBuf.frameCount * mChannelCount,
               (mProcFramesIn - inBuf.frameCount) * mChannelCount * sizeof(int16_t));
        mProcFramesIn -= inBuf.frameCount;

        // if not enough frames were passed to process(), read more and retry.
        if (outBuf.frameCount == 0) {
            continue;
        }
        framesWr += outBuf.frameCount;
    }
    return framesWr;
}

ssize_t AudioHardware::AudioStreamInALSA::read(void* buffer, ssize_t bytes)
{
    //    LOGV("AudioStreamInALSA::read(%p, %d)", buffer, (int)bytes);
    status_t status = NO_INIT;

    if (mHardware == NULL) return NO_INIT;

    if (mSleepReq) {
        // 10ms are always shorter than the time to reconfigure the audio path
        // which is the only condition when mSleepReq would be true.
        usleep(10000);
    }

    { // scope for the lock
        AutoMutex lock(mLock);

        if (mStandby) {
            AutoMutex hwLock(mHardware->lock());

            LOGD("AudioHardware pcm capture is exiting standby.");
            acquire_wake_lock (PARTIAL_WAKE_LOCK, "AudioInLock");

            sp<AudioStreamOutALSA> spOut = mHardware->output();
            while (spOut != 0) {
                if (!spOut->checkStandby()) {
                    int cnt = spOut->prepareLock();
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

        size_t framesRq = bytes / mChannelCount/sizeof(int16_t);
        ssize_t framesRd;

        if (mPreprocessors.size() == 0) {
            framesRd = readFrames(buffer, framesRq);
        } else {
            framesRd = processFrames(buffer, framesRq);
        }

        if (framesRd >= 0) {
            return framesRd * mChannelCount * sizeof(int16_t);
        }

        LOGW("read error: %d", (int)framesRd);
        status = framesRd;
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

    mSleepReq = true;
    {
        AutoMutex lock(mLock);
        mSleepReq = false;

        { // scope for AudioHardware lock
            AutoMutex hwLock(mHardware->lock());

            doStandby_l();
        }
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

    delete[] mProcBuf;
    mProcBuf = NULL;
    mProcBufSize = 0;
}

status_t AudioHardware::AudioStreamInALSA::open_l()
{
    unsigned flags = PCM_IN;

    struct pcm_config config = {
        channels : mChannelCount,
        rate : AUDIO_HW_IN_SAMPLERATE,
        period_size : AUDIO_HW_IN_PERIOD_SZ,
        period_count : AUDIO_HW_IN_PERIOD_CNT,
        format : PCM_FORMAT_S16_LE,
    };

    LOGV("open pcm_in driver");
    TRACE_DRIVER_IN(DRV_PCM_OPEN)
    mPcm = pcm_open(0, 0, flags, &config);
    TRACE_DRIVER_OUT
    if (!pcm_is_ready(mPcm)) {
        LOGE("cannot open pcm_in driver: %s\n", pcm_get_error(mPcm));
        TRACE_DRIVER_IN(DRV_PCM_CLOSE)
        pcm_close(mPcm);
        TRACE_DRIVER_OUT
        mPcm = NULL;
        return NO_INIT;
    }

    if (mDownSampler != NULL) {
        mDownSampler->reset();
    }
    mInputFramesIn = 0;

    mProcBufSize = 0;
    mProcFramesIn = 0;

    mMixer = mHardware->openMixer_l();
    if (mMixer) {
        TRACE_DRIVER_IN(DRV_MIXER_GET)
        mRouteCtl = mixer_get_ctl_by_name(mMixer, "Capture MIC Path");
        TRACE_DRIVER_OUT
    }

    if (mHardware->mode() != AudioSystem::MODE_IN_CALL) {
        const char *route = mHardware->getInputRouteFromDevice(mDevices);
        LOGV("read() wakeup setting route %s", route);
        if (mRouteCtl) {
            TRACE_DRIVER_IN(DRV_MIXER_SEL)
            mixer_ctl_set_enum_by_string(mRouteCtl, route);
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

    LOGD("AudioStreamInALSA::setParameters() %s", keyValuePairs.string());

    if (mHardware == NULL) return NO_INIT;

    mSleepReq = true;
    {
        AutoMutex lock(mLock);
        mSleepReq = false;

        if (param.getInt(String8(AudioParameter::keyInputSource), value) == NO_ERROR) {
            AutoMutex hwLock(mHardware->lock());

            mHardware->openMixer_l();
            mHardware->setInputSource_l((audio_source)value);
            mHardware->closeMixer_l();

            param.remove(String8(AudioParameter::keyInputSource));
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

status_t AudioHardware::AudioStreamInALSA::addAudioEffect(effect_handle_t effect)
{
    LOGV("AudioStreamInALSA::addAudioEffect() %p", effect);

    AutoMutex lock(mLock);
    mPreprocessors.add(effect);
    return NO_ERROR;
}

status_t AudioHardware::AudioStreamInALSA::removeAudioEffect(effect_handle_t effect)
{
    status_t status = INVALID_OPERATION;
    LOGV("AudioStreamInALSA::removeAudioEffect() %p", effect);
    {
        AutoMutex lock(mLock);
        for (size_t i = 0; i < mPreprocessors.size(); i++) {
            if (mPreprocessors[i] == effect) {
                mPreprocessors.removeAt(i);
                status = NO_ERROR;
                break;
            }
        }
    }

    return status;
}

status_t AudioHardware::AudioStreamInALSA::getNextBuffer(AudioHardware::BufferProvider::Buffer* buffer)
{
    if (mPcm == NULL) {
        buffer->raw = NULL;
        buffer->frameCount = 0;
        mReadStatus = NO_INIT;
        return NO_INIT;
    }

    if (mInputFramesIn == 0) {
        TRACE_DRIVER_IN(DRV_PCM_READ)
        mReadStatus = pcm_read(mPcm,(void*) mInputBuf, AUDIO_HW_IN_PERIOD_SZ * frameSize());
        TRACE_DRIVER_OUT
        if (mReadStatus != 0) {
            buffer->raw = NULL;
            buffer->frameCount = 0;
            return mReadStatus;
        }
        mInputFramesIn = AUDIO_HW_IN_PERIOD_SZ;
    }

    buffer->frameCount = (buffer->frameCount > mInputFramesIn) ? mInputFramesIn : buffer->frameCount;
    buffer->i16 = mInputBuf + (AUDIO_HW_IN_PERIOD_SZ - mInputFramesIn) * mChannelCount;

    return mReadStatus;
}

void AudioHardware::AudioStreamInALSA::releaseBuffer(AudioHardware::BufferProvider::Buffer* buffer)
{
    mInputFramesIn -= buffer->frameCount;
}

size_t AudioHardware::AudioStreamInALSA::getBufferSize(uint32_t sampleRate, int channelCount)
{
    size_t i;
    size_t size = sizeof(inputConfigTable)/sizeof(uint32_t)/INPUT_CONFIG_CNT;

    for (i = 0; i < size; i++) {
        if (sampleRate == inputConfigTable[i][INPUT_CONFIG_SAMPLE_RATE]) {
            return (AUDIO_HW_IN_PERIOD_SZ*channelCount*sizeof(int16_t)) /
                    inputConfigTable[i][INPUT_CONFIG_BUFFER_RATIO];
        }
    }
    // this should never happen as getBufferSize() is always called after getInputSampleRate()
    // that checks for valid sampling rates.
    LOGE("AudioStreamInALSA::getBufferSize() invalid sampling rate %d", sampleRate);
    return 0;
}

int AudioHardware::AudioStreamInALSA::prepareLock()
{
    // request sleep next time read() is called so that caller can acquire
    // mLock
    mSleepReq = true;
    return mStandbyCnt;
}

void AudioHardware::AudioStreamInALSA::lock()
{
    mLock.lock();
    mSleepReq = false;
}

void AudioHardware::AudioStreamInALSA::unlock() {
    mLock.unlock();
}

//------------------------------------------------------------------------------
// speex based resampler
//------------------------------------------------------------------------------

#define RESAMPLER_QUALITY 2

AudioHardware::ReSampler::ReSampler(uint32_t inSampleRate,
                                    uint32_t outSampleRate,
                                    uint32_t channelCount,
                                    BufferProvider* provider)
    :  mStatus(NO_INIT), mSpeexResampler(NULL), mProvider(provider),
       mInSampleRate(inSampleRate), mOutSampleRate(outSampleRate), mChannelCount(channelCount),
       mInBuf(NULL), mInBufSize(0)
{
    LOGV("AudioHardware::ReSampler() cstor %p In SR %d Out SR %d channels %d",
         this, mInSampleRate, mInSampleRate, mChannelCount);

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

    mStatus = NO_ERROR;
}

AudioHardware::ReSampler::~ReSampler()
{
    free(mInBuf);

    if (mSpeexResampler != NULL) {
        speex_resampler_destroy(mSpeexResampler);
    }
}

void AudioHardware::ReSampler::reset()
{
    mFramesIn = 0;
    mFramesRq = 0;

    if (mSpeexResampler != NULL) {
        speex_resampler_reset_mem(mSpeexResampler);
    }
}

// outputs a number of frames less or equal to *outFrameCount and updates *outFrameCount
// with the actual number of frames produced.
int AudioHardware::ReSampler::resample(int16_t *out, size_t *outFrameCount)
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
            AudioHardware::BufferProvider::Buffer buf;
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


//------------------------------------------------------------------------------
//  Factory
//------------------------------------------------------------------------------

extern "C" AudioHardwareInterface* createAudioHardware(void) {
    return new AudioHardware();
}

}; // namespace android
