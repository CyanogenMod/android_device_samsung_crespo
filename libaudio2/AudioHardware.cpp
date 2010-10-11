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

// ----------------------------------------------------------------------------

AudioHardware::AudioHardware() :
    mInit(false),
    mMicMute(false),
    mOutput(NULL),
    mPcm(NULL),
    mMixer(NULL),
    mPcmOpenCnt(0),
    mMixerOpenCnt(0),
    mVrModeEnabled(false),
    mBluetoothNrec(true),
    mSecRilLibHandle(NULL),
    mRilClient(0),
    mActivatedCP(false)
{
    loadRILD();
    mInit = true;
}

AudioHardware::~AudioHardware()
{
    for (size_t index = 0; index < mInputs.size(); index++) {
        closeInputStream((AudioStreamIn*)mInputs[index]);
    }
    mInputs.clear();
    closeOutputStream((AudioStreamOut*)mOutput);

    if (mMixer) {
        mixer_close(mMixer);
    }
    if (mPcm) {
        pcm_close(mPcm);
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
    AudioStreamOutALSA* out = NULL;
    status_t rc;

    { // scope for the lock
        Mutex::Autolock lock(mLock);

        // only one output stream allowed
        if (mOutput) {
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
        if (out) {
            delete out;
        }
        out = NULL;
    }
    if (status) {
        *status = rc;
    }

    return out;
}

void AudioHardware::closeOutputStream(AudioStreamOut* out) {
    {
        Mutex::Autolock lock(mLock);
        if (mOutput == 0 || mOutput != out) {
            LOGW("Attempt to close invalid output stream");
            return;
        }
        mOutput = 0;
    }
    delete out;
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
    AudioStreamInALSA* in = NULL;;

    { // scope for the lock
        Mutex::Autolock lock(mLock);

        in = new AudioStreamInALSA();
        rc = in->set(this, devices, format, channels, sampleRate, acoustic_flags);
        if (rc == NO_ERROR) {
            mInputs.add(in);
        }
    }

    if (rc != NO_ERROR) {
        if (in) {
            delete in;
        }
        in = NULL;
    }
    if (status) {
        *status = rc;
    }

    LOGV("AudioHardware::openInputStream()%p", in);
    return in;
}

void AudioHardware::closeInputStream(AudioStreamIn* in) {
    {
        Mutex::Autolock lock(mLock);

        ssize_t index = mInputs.indexOf((AudioStreamInALSA *)in);
        if (index < 0) {
            LOGW("Attempt to close invalid input stream");
            return;
        }
        mInputs.removeAt(index);
    }
    LOGV("AudioHardware::closeInputStream()%p", in);
    delete in;
}


status_t AudioHardware::setMode(int mode)
{
    AutoMutex lock(mLock);
    int prevMode = mMode;
    status_t status = AudioHardwareBase::setMode(mode);
    LOGV("setMode() : new %d, old %d", mMode, prevMode);
    if (status == NO_ERROR) {
        // make sure that doAudioRouteOrMute() is called by doRouting()
        // when entering or exiting in call mode even if the new device
        // selected is the same as current one.
        if ((mMode == AudioSystem::MODE_RINGTONE) || (mMode == AudioSystem::MODE_IN_CALL))
        {
            if ((!mActivatedCP) && (mSecRilLibHandle) && (connectRILDIfRequired() == OK)) {
                setCallClockSync(mRilClient, SOUND_CLOCK_START);
                mActivatedCP = true;
            }
        }

        if (((prevMode != AudioSystem::MODE_IN_CALL) && (mMode == AudioSystem::MODE_IN_CALL)) ||
            ((prevMode == AudioSystem::MODE_IN_CALL) && (mMode != AudioSystem::MODE_IN_CALL))) {
            if (mMode == AudioSystem::MODE_IN_CALL) {
                openPcmOut_l();
                openMixer_l();
                setVoiceRecognition_l(false);
            }
            setIncallPath_l(mOutput->device());
            if (mMode != AudioSystem::MODE_IN_CALL) {
                setVoiceRecognition_l(mVrModeEnabled);
                closeMixer_l();
                mOutput->setNextRoute(getOutputRouteFromDevice(mOutput->device()));
                closePcmOut_l();
                AudioStreamInALSA *input = getActiveInput_l();
                if (input != NULL) {
                    input->setNextRoute(getInputRouteFromDevice(input->device()));
                }
            }
        }

        if (mMode == AudioSystem::MODE_NORMAL) {
            if(mActivatedCP)
                mActivatedCP = false;
        }
    }

    return status;
}

status_t AudioHardware::setMicMute(bool state)
{
    LOGV("setMicMute(%d) mMicMute %d", state, mMicMute);
    AutoMutex lock(mLock);
    if (mMicMute != state) {
        mMicMute = state;
        if (mMode != AudioSystem::MODE_IN_CALL) {
            AudioStreamInALSA *input = getActiveInput_l();
            if (input != NULL) {
                input->setNextRoute(getInputRouteFromDevice(input->device()));
            }
        }
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
            LOGI("Turning noise reduction and echo cancellation off for BT "
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
    LOGI("### setVoiceVolume");

    AutoMutex lock(mLock);
    if ( (AudioSystem::MODE_IN_CALL == mMode) && (mSecRilLibHandle) &&
         (connectRILDIfRequired() == OK) ) {

        uint32_t device = AudioSystem::DEVICE_OUT_EARPIECE;
        if (mOutput != NULL) {
            device = mOutput->device();
        }
        int int_volume = (int)(volume * 5);
        SoundType type;

        LOGI("### route(%d) call volume(%f)", device, volume);
        switch (device) {
            case AudioSystem::DEVICE_OUT_EARPIECE:
                LOGI("### earpiece call volume");
                type = SOUND_TYPE_VOICE;
                break;

            case AudioSystem::DEVICE_OUT_SPEAKER:
                LOGI("### speaker call volume");
                type = SOUND_TYPE_SPEAKER;
                break;

            case AudioSystem::DEVICE_OUT_BLUETOOTH_SCO:
            case AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_HEADSET:
            case AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_CARKIT:
                LOGI("### bluetooth call volume");
                type = SOUND_TYPE_BTVOICE;
                break;

            case AudioSystem::DEVICE_OUT_WIRED_HEADSET:
            case AudioSystem::DEVICE_OUT_WIRED_HEADPHONE: // Use receive path with 3 pole headset.
                LOGI("### headset call volume");
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

status_t AudioHardware::dump(int fd, const Vector<String16>& args)
{
    return NO_ERROR;
}

status_t AudioHardware::setIncallPath(uint32_t device) {
    Mutex::Autolock lock(mLock);
    return AudioHardware::setIncallPath_l(device);
}

status_t AudioHardware::setIncallPath_l(uint32_t device)
{
    LOGV("setIncallPath: device %x", device);

    // Setup sound path for CP clocking
    if ((mSecRilLibHandle) &&
        (connectRILDIfRequired() == OK)) {

        if (mMode == AudioSystem::MODE_IN_CALL) {
            LOGI("### incall mode route (%d)", device);
            AudioPath path;
            switch(device){
                case AudioSystem::DEVICE_OUT_EARPIECE:
                    LOGI("### incall mode earpiece route");
                    path = SOUND_AUDIO_PATH_HANDSET;
                    break;

                case AudioSystem::DEVICE_OUT_SPEAKER:
                    LOGI("### incall mode speaker route");
                    path = SOUND_AUDIO_PATH_SPEAKER;
                    break;

                case AudioSystem::DEVICE_OUT_BLUETOOTH_SCO:
                case AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_HEADSET:
                case AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_CARKIT:
                    LOGI("### incall mode bluetooth route %s NR", mBluetoothNrec ? "" : "NO");
                    if (mBluetoothNrec) {
                        path = SOUND_AUDIO_PATH_BLUETOOTH;
                    } else {
                        path = SOUND_AUDIO_PATH_BLUETOOTH_NO_NR;
                    }
                    break;

                case AudioSystem::DEVICE_OUT_WIRED_HEADSET :
                case AudioSystem::DEVICE_OUT_WIRED_HEADPHONE :
                    LOGI("### incall mode headset route");
                    path = SOUND_AUDIO_PATH_HEADSET;
                    break;

                default:
                    LOGW("### incall mode Error!! route = [%d]", device);
                    path = SOUND_AUDIO_PATH_HANDSET;
                    break;
            }

            setCallAudioPath(mRilClient, path);

            if (mMixer != NULL) {
                struct mixer_ctl *ctl= mixer_get_control(mMixer, "Voice Call Path", 0);
                LOGE_IF(ctl == NULL, "setIncallPath_l() could not get mixer ctl");
                if (ctl != NULL) {
                    LOGV("setIncallPath_l() Voice Call Path, (%x)", device);
                    mixer_ctl_select(ctl, getVoiceRouteFromDevice(device));
                }
            }
        }
    }
    return NO_ERROR;
}

struct pcm *AudioHardware::openPcmOut()
{
    AutoMutex lock(mLock);
    return openPcmOut_l();
}

struct pcm *AudioHardware::openPcmOut_l()
{
    LOGI("openPcmOut_l() mPcmOpenCnt: %d", mPcmOpenCnt);
    if (mPcmOpenCnt++ == 0) {
        if (mPcm != NULL) {
            LOGE("openPcmOut_l() mPcmOpenCnt == 0 and mPcm == %p\n", mPcm);
            mPcmOpenCnt--;
            return NULL;
        }
        unsigned flags = PCM_OUT;

        flags |= (AUDIO_HW_OUT_PERIOD_MULT - 1) << PCM_PERIOD_SZ_SHIFT;
        flags |= (AUDIO_HW_OUT_PERIOD_CNT - PCM_PERIOD_CNT_MIN) << PCM_PERIOD_CNT_SHIFT;

        mPcm = pcm_open(flags);
        if (!pcm_ready(mPcm)) {
            LOGE("openPcmOut_l() cannot open pcm_out driver: %s\n", pcm_error(mPcm));
            pcm_close(mPcm);
            mPcm = 0;
        }
    }
    return mPcm;
}

void AudioHardware::closePcmOut()
{
    AutoMutex lock(mLock);
    closePcmOut_l();
}

void AudioHardware::closePcmOut_l()
{
    LOGI("closePcmOut_l() mPcmOpenCnt: %d", mPcmOpenCnt);
    if (mPcmOpenCnt == 0) {
        LOGE("closePcmOut_l() mPcmOpenCnt == 0");
        return;
    }

    if (--mPcmOpenCnt == 0) {
        pcm_close(mPcm);
        mPcm = NULL;
    }
}

struct mixer *AudioHardware::openMixer()
{
    AutoMutex lock(mLock);
    return openMixer_l();
}

struct mixer *AudioHardware::openMixer_l()
{
    LOGV("openMixer_l() mMixerOpenCnt: %d", mMixerOpenCnt);
    if (mMixerOpenCnt++ == 0) {
        if (mMixer != NULL) {
            LOGE("openMixer_l() mMixerOpenCnt == 0 and mMixer == %p\n", mPcm);
            mMixerOpenCnt--;
            return NULL;
        }
        mMixer = mixer_open();
        LOGE_IF(mMixer == NULL, "openMixer_l() cannot open mixer");
    }
    return mMixer;
}

void AudioHardware::closeMixer()
{
    AutoMutex lock(mLock);
    closeMixer_l();
}

void AudioHardware::closeMixer_l()
{
    LOGV("closeMixer_l() mMixerOpenCnt: %d", mMixerOpenCnt);
    if (mMixerOpenCnt == 0) {
        LOGE("closeMixer_l() mMixerOpenCnt == 0");
        return;
    }

    if (--mMixerOpenCnt == 0) {
        mixer_close(mMixer);
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
//uncomment when kernel change is submitted
//        if (mMode == AudioSystem::MODE_RINGTONE) return "RING_HP_NO_MIC";
//        else return "HP_NO_MIC";
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
//        return "HP_NO_MIC";
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
AudioHardware::AudioStreamInALSA *AudioHardware::getActiveInput_l()
{
    for (size_t i = 0; i < mInputs.size(); i++) {
        // return first input found not being in standby mode
        // as only one input can be in this state
        if (!mInputs[i]->checkStandby()) {
            return mInputs[i];
        }
    }

    return NULL;
}


status_t AudioHardware::setVoiceRecognition(bool enable)
{
    AutoMutex lock(mLock);
    return setVoiceRecognition_l(enable);
}

status_t AudioHardware::setVoiceRecognition_l(bool enable)
{
     LOGV("setVoiceRecognition_l(%d)", enable);
     if (enable != mVrModeEnabled) {
         if (!(enable && (mMode == AudioSystem::MODE_IN_CALL))) {
             openMixer_l();
             if (mMixer) {
                 struct mixer_ctl *ctl= mixer_get_control(mMixer, "Recognition Control", 0);
                 if (ctl == NULL) {
                     closeMixer_l();
                     return NO_INIT;
                 }
                 const char *mode = enable ? "RECOGNITION_ON" : "RECOGNITION_OFF";
                 LOGV("mixer_ctl_select, Recognition Control, (%s)", mode);
                 mixer_ctl_select(ctl, mode);
             }
             closeMixer_l();
         }
         mVrModeEnabled = enable;
     }

     return NO_ERROR;
}


//------------------------------------------------------------------------------
//  AudioStreamOutALSA
//------------------------------------------------------------------------------

AudioHardware::AudioStreamOutALSA::AudioStreamOutALSA() :
    mHardware(0), mPcm(0), mMixer(0), mRouteCtl(0), mStartCount(0),
    mStandby(true), mDevices(0), mChannels(AUDIO_HW_OUT_CHANNELS),
    mSampleRate(AUDIO_HW_OUT_SAMPLERATE), mBufferSize(AUDIO_HW_OUT_PERIOD_BYTES)
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

    AutoMutex lock(mLock);
    if (mStandby) {
        LOGV("open pcm_out driver");

        mPcm = mHardware->openPcmOut();
        if (!pcm_ready(mPcm)) {
            LOGE("cannot open pcm_out driver: %s\n", pcm_error(mPcm));
            mHardware->closePcmOut();
            mPcm = 0;
            goto Error;
        }

        mMixer = mixer_open();
        if (mMixer) {
            LOGV("open playback normal");
            mRouteCtl = mixer_get_control(mMixer, "Playback Path", 0);
        }
        if (mHardware->mode() == AudioSystem::MODE_IN_CALL) {
            next_route = 0;
        } else {
            next_route = mHardware->getOutputRouteFromDevice(mDevices);
        }
        acquire_wake_lock (PARTIAL_WAKE_LOCK, "AudioOutLock");
        mStandby = false;
    }

    ret = pcm_write(mPcm,(void*) p, bytes);

    // FIXME:: this is done here as setting the route doesn't seem to work before reading
    // first buffer
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
    AutoMutex lock(mLock);
    if (!mStandby) {
        if (next_route && mRouteCtl) {
            mixer_ctl_select(mRouteCtl, next_route);
            next_route = 0;
        }
        release_wake_lock("AudioOutLock");
        mStandby = true;
        LOGV("AudioHardware pcm playback is going to standby.");
    }

    if (mMixer) {
        mixer_close(mMixer);
        mMixer = 0;
        if (mRouteCtl) {
            mRouteCtl = 0;
        }
    }
    if (mPcm) {
        mHardware->closePcmOut();
        mPcm = 0;
    }
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
    AudioParameter param = AudioParameter(keyValuePairs);
    status_t status = NO_ERROR;
    int device;
    LOGD("AudioStreamOutALSA::setParameters() %s", keyValuePairs.string());

    if (mHardware == NULL) return NO_INIT;

    if (param.getInt(String8(AudioParameter::keyRouting), device) == NO_ERROR)
    {
        if (mDevices != (uint32_t)device) {
            mDevices = (uint32_t)device;
            if (mHardware->mode() != AudioSystem::MODE_IN_CALL) {
                next_route = mHardware->getOutputRouteFromDevice(device);
            }
        }
        if (mHardware->mode() == AudioSystem::MODE_IN_CALL) {
            mHardware->setIncallPath(device);
        }

        param.remove(String8(AudioParameter::keyRouting));
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
//  AudioStreamOutALSA
//------------------------------------------------------------------------------

AudioHardware::AudioStreamInALSA::AudioStreamInALSA() :
    mHardware(0), mPcm(0), mMixer(0), mRouteCtl(0), mStartCount(0),
    mStandby(true), mDevices(0), mChannels(AUDIO_HW_IN_CHANNELS), mChannelCount(1),
    mSampleRate(AUDIO_HW_IN_SAMPLERATE), mBufferSize(AUDIO_HW_IN_PERIOD_BYTES),
    mDownSampler(NULL), mReadStatus(NO_ERROR)
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

    AutoMutex lock(mLock);

    if (mStandby) {
        LOGV("open pcm_in driver");

        unsigned flags = PCM_IN;
        if (mChannels == AudioSystem::CHANNEL_IN_MONO) {
            flags |= PCM_MONO;
        }
        flags |= (AUDIO_HW_IN_PERIOD_MULT - 1) << PCM_PERIOD_SZ_SHIFT;
        flags |= (AUDIO_HW_IN_PERIOD_CNT - PCM_PERIOD_CNT_MIN) << PCM_PERIOD_CNT_SHIFT;

        mPcm = pcm_open(flags);
        if (!pcm_ready(mPcm)) {
            LOGE("cannot open pcm_out driver: %s\n", pcm_error(mPcm));
            pcm_close(mPcm);
            mPcm = 0;
            goto Error;
        }

        mMixer = mixer_open();
        if (mMixer) {
            mRouteCtl = mixer_get_control(mMixer, "Capture MIC Path", 0);
            mMicCtl = mixer_get_control(mMixer, "Mic Status", 0);
            if (mMicCtl) {
                LOGV("turn mic ON");
                mixer_ctl_select(mRouteCtl, "MIC_USE");
            }
        }

        if (mDownSampler != NULL) {
            mInPcmInBuf = 0;
            mDownSampler->reset();
        }
        if (mHardware->mode() != AudioSystem::MODE_IN_CALL) {
            next_route = mHardware->getInputRouteFromDevice(mDevices);
        }
        acquire_wake_lock (PARTIAL_WAKE_LOCK, "AudioInLock");
        mStandby = false;
    }

    if (mDownSampler != NULL) {
        size_t frames = bytes / frameSize();
        size_t framesIn = 0;
        mReadStatus = 0;
        do {
            size_t outframes = frames - framesIn;
            mDownSampler->resample((int16_t *)buffer + (framesIn * mChannelCount), &outframes);
            framesIn += outframes;
        } while ((framesIn < frames) && mReadStatus == 0);
        ret = mReadStatus;
        bytes = framesIn * frameSize();
    } else {
        ret = pcm_read(mPcm, buffer, bytes);
    }

    // FIXME:: this is done here as setting the route doesn't seem to work before reading
    // first buffer
    if (ret == 0) {
        if (next_route && mRouteCtl) {
            LOGV("set route : %s ", next_route);
            mixer_ctl_select(mRouteCtl, next_route);
            next_route = 0;
        }
        return bytes;
    }

    LOGW("read error: %d", ret);
    status = ret;

Error:
    standby();

    // Simulate audio output timing in case of error
    usleep(bytes * 1000000 / frameSize() / sampleRate());

    return status;
}

status_t AudioHardware::AudioStreamInALSA::standby()
{
    AutoMutex lock(mLock);

    if (!mStandby) {
        if (next_route && mRouteCtl) {
            mixer_ctl_select(mRouteCtl, next_route);
            next_route = 0;
        }
        release_wake_lock("AudioInLock");
        mStandby = true;
        LOGI("AudioHardware pcm playback is going to standby.");
    }

    if (mPcm) {
        pcm_close(mPcm);
        mPcm = 0;
    }
    if (mMixer) {
        if (mMicCtl) {
            LOGV("turn mic OFF");
            mixer_ctl_select(mMicCtl, "MIC_NO_USE");
            mMicCtl = 0;
        }
        mixer_close(mMixer);
        mMixer = 0;
        if (mRouteCtl) {
            mRouteCtl = 0;
        }

    }
    return NO_ERROR;
}

status_t AudioHardware::AudioStreamInALSA::dump(int fd, const Vector<String16>& args)
{
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

    if (param.getInt(String8(VOICE_REC_MODE_KEY), value) == NO_ERROR) {
        mHardware->setVoiceRecognition((value != 0));
        param.remove(String8(VOICE_REC_MODE_KEY));
    }

    if (param.getInt(String8(AudioParameter::keyRouting), value) == NO_ERROR)
    {
        if (value != 0) {
            if (mDevices != (uint32_t)value) {
                if (mHardware->mode() != AudioSystem::MODE_IN_CALL) {
                    next_route = mHardware->getInputRouteFromDevice((uint32_t)value);
                }
                mDevices = (uint32_t)value;
            }
        }
        param.remove(String8(AudioParameter::keyRouting));
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
        return NO_INIT;
    }

    if (mInPcmInBuf == 0) {
        mReadStatus = pcm_read(mPcm,(void*) mPcmIn, AUDIO_HW_IN_PERIOD_SZ * frameSize());
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
