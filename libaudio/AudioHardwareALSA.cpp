/* AudioHardwareALSA.cpp
 **
 ** Copyright 2008-2009 Wind River Systems
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
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#define LOG_TAG "AudioHardwareALSA"
#include <utils/Log.h>
#include <utils/String8.h>

#include <cutils/properties.h>
#include <media/AudioRecord.h>
#include <hardware_legacy/power.h>

#include <alsa/asoundlib.h>
#include "AudioHardwareALSA.h"
#define READ_FRAME_SIZE 2080
#define READ_FRAME_SIZE_STANDARD 4160

#if defined SEC_IPC
// sangsu fix : headers for IPC
#include <telephony/ril.h>
#endif
#ifndef ALSA_DEFAULT_SAMPLE_RATE
#define ALSA_DEFAULT_SAMPLE_RATE 44100 // in Hz
#endif

#define SND_MIXER_VOL_RANGE_MIN  (0)
#define SND_MIXER_VOL_RANGE_MAX  (100)

#define ALSA_NAME_MAX 128

#define ALSA_STRCAT(x,y) \
    if (strlen(x) + strlen(y) < ALSA_NAME_MAX) \
        strcat(x, y);

#define PERIOD_PLAYBACK 	4	
#define PERIOD_CAPTURE	 	4
#define PLAYBACK	0
#define CAPTURE		1

// If you want to dump PCM data, activate this feature
//#define PCM_INPUT_DUMP
//#define PCM_OUTPUT_DUMP

#ifdef PCM_INPUT_DUMP
#define PCM_INPUT_DUMP_PATH	"/data/Read_PCM_Dump.dat"
FILE *fpInput = NULL ;
#endif

#ifdef PCM_OUTPUT_DUMP
#define PCM_OUTPUT_DUMP_PATH	"/data/Write_PCM_Dump.dat"
FILE *fpOutput = NULL ;
#endif

extern "C"
{
    extern int ffs(int i);

    //
    // Make sure this prototype is consistent with what's in
    // external/libasound/alsa-lib-1.0.16/src/pcm/pcm_null.c!
    //
    extern int snd_pcm_null_open(snd_pcm_t **pcmp,
                                 const char *name,
                                 snd_pcm_stream_t stream,
                                 int mode);

    //
    // Function for dlsym() to look up for creating a new AudioHardwareInterface.
    //
    android::AudioHardwareInterface *createAudioHardware(void) {
        return new android::AudioHardwareALSA();
    }
}         // extern "C"

namespace android
{

#if 0
typedef AudioSystem::audio_routes audio_routes;
#define ROUTE_ALL            AudioSystem::ROUTE_ALL
#define ROUTE_EARPIECE       AudioSystem::ROUTE_EARPIECE
#define ROUTE_SPEAKER        AudioSystem::ROUTE_SPEAKER
#define ROUTE_BLUETOOTH_SCO  AudioSystem::ROUTE_BLUETOOTH_SCO
#define ROUTE_HEADSET        AudioSystem::ROUTE_HEADSET
#define ROUTE_BLUETOOTH_A2DP AudioSystem::ROUTE_BLUETOOTH_A2DP
#elif defined SEC_SWP_SOUND
typedef AudioSystem::audio_devices audio_routes;
#define ROUTE_ALL            AudioSystem::DEVICE_OUT_ALL
#define ROUTE_EARPIECE       AudioSystem::DEVICE_OUT_EARPIECE
#define ROUTE_HEADSET        AudioSystem::DEVICE_OUT_WIRED_HEADSET
#define ROUTE_HEADPHONE	AudioSystem::DEVICE_OUT_WIRED_HEADPHONE
#define ROUTE_SPEAKER        AudioSystem::DEVICE_OUT_SPEAKER
#define ROUTE_BLUETOOTH_SCO  AudioSystem::DEVICE_OUT_BLUETOOTH_SCO
#define ROUTE_BLUETOOTH_SCO_HEADSET  AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_HEADSET
#define ROUTE_BLUETOOTH_SCO_CARKIT  AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_CARKIT
#define ROUTE_BLUETOOTH_A2DP AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP
#define ROUTE_BLUETOOTH_A2DP_HEADPHONES AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES
#define ROUTE_BLUETOOTH_A2DP_SPEAKER AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER
#else
typedef AudioSystem::audio_devices audio_routes;
#define ROUTE_ALL            AudioSystem::DEVICE_OUT_ALL
#define ROUTE_EARPIECE       AudioSystem::DEVICE_OUT_EARPIECE
#define ROUTE_SPEAKER        AudioSystem::DEVICE_OUT_SPEAKER
#define ROUTE_BLUETOOTH_SCO  AudioSystem::DEVICE_OUT_BLUETOOTH_SCO
#define ROUTE_HEADSET        AudioSystem::DEVICE_OUT_WIRED_HEADSET
#define ROUTE_BLUETOOTH_A2DP AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP
#endif
// ----------------------------------------------------------------------------

static const int DEFAULT_SAMPLE_RATE = ALSA_DEFAULT_SAMPLE_RATE;

static const char _nullALSADeviceName[] = "NULL_Device";

static void ALSAErrorHandler(const char *file,
                             int line,
                             const char *function,
                             int err,
                             const char *fmt,
                             ...)
{
    char buf[BUFSIZ];
    va_list arg;
    int l;

    va_start(arg, fmt);
    l = snprintf(buf, BUFSIZ, "%s:%i:(%s) ", file, line, function);
    vsnprintf(buf + l, BUFSIZ - l, fmt, arg);
    buf[BUFSIZ-1] = '\0';
    LOGE("ALSALib %s.", buf);
    va_end(arg);
}

// ----------------------------------------------------------------------------

/* The following table(s) need to match in order of the route bits
 */
#if defined SEC_SWP_SOUND
static const char *deviceSuffix[] = {
	// output devices
	/* ROUTE_EARPIECE       */ "_Earpiece",
	/* ROUTE_SPEAKER        */ "_Speaker",
	/* ROUTE_HEADSET        */ "_Headset",
	/* ROUTE_HEADPHONE  */ "_Headset",
	/* ROUTE_BLUETOOTH_SCO	*/ "_Bluetooth",
	/* ROUTE_BLUETOOTH_SCO_HEADSET	*/ "_Bluetooth", 
	/* ROUTE_BLUETOOTH_SCO_CARKIT  */ "_Bluetooth", //"_Bluetooth_Carkit"
	/* ROUTE_BLUETOOTH_A2DP */ "_Bluetooth", //"_Bluetooth-A2DP"
	/* ROUTE_BLUETOOTH_A2DP_HEADPHONES */ "_Bluetooth", //"_Bluetooth-A2DP_HeadPhone"
	/* ROUTE_BLUETOOTH_A2DP_SPEAKER */ "_Bluetooth", 	// "_Bluetooth-A2DP_Speaker"
	/* ROUTE_AUX_DIGITAL */ "_AuxDigital",
	/* ROUTE_TV_OUT */ "_TvOut",
	/* ROUTE_AUX_DIGITAL */ "_ExtraDockSpeaker",
	/* ROUTE_NULL */ "_Null",
	/* ROUTE_NULL */ "_Null",
	/* ROUTE_DEFAULT */ "_OutDefault",
		
	// input devices
	/* ROUTE_COMMUNICATION		 */ "_Communication",
	/* ROUTE_AMBIENT		*/ "_Ambient",
	/* ROUTE_BUILTIN_MIC		*/ "_Speaker",
	/* ROUTE_BLUETOOTH_SCO_HEADSET		  */ "_Bluetooth",
	/* ROUTE_WIRED_HEADSET	*/ "_Headset",
	/* ROUTE_AUX_DIGITAL  */ "_AuxDigital",
	/* ROUTE_VOICE_CALL  */ "_VoiceCall",
	/* ROUTE_BACK_MIC */ "_BackMic",
	/* ROUTE_IN_DEFAULT  */ "_InDefault",
};
#else
static const char *deviceSuffix[] = {
    /* ROUTE_EARPIECE       */ "_Earpiece",
    /* ROUTE_SPEAKER        */ "_Speaker",
    /* ROUTE_BLUETOOTH_SCO  */ "_Bluetooth",
    /* ROUTE_HEADSET        */ "_Headset",
    /* ROUTE_BLUETOOTH_A2DP */ "_Bluetooth-A2DP",
};
#endif

static const int deviceSuffixLen = (sizeof(deviceSuffix) / sizeof(char *));

struct mixer_info_t;

struct alsa_properties_t
{
    const audio_routes  routes;
    const char         *propName;
    const char         *propDefault;
    mixer_info_t       *mInfo;
};

static alsa_properties_t masterPlaybackProp = {
    ROUTE_ALL, "alsa.mixer.playback.master", "PCM", NULL
};

static alsa_properties_t masterCaptureProp = {
    ROUTE_ALL, "alsa.mixer.capture.master", "Capture", NULL
};

static alsa_properties_t
mixerMasterProp[SND_PCM_STREAM_LAST+1] = {
    { ROUTE_ALL, "alsa.mixer.playback.master",  "PCM",     NULL},
    { ROUTE_ALL, "alsa.mixer.capture.master",   "Capture", NULL}
};

static alsa_properties_t
mixerProp[][SND_PCM_STREAM_LAST+1] = {
    {
        {ROUTE_EARPIECE,       "alsa.mixer.playback.earpiece",       "Earpiece", NULL},
        {ROUTE_EARPIECE,       "alsa.mixer.capture.earpiece",        "Capture",  NULL}
    },
    {
        {ROUTE_SPEAKER,        "alsa.mixer.playback.speaker",        "Speaker", NULL},
        {ROUTE_SPEAKER,        "alsa.mixer.capture.speaker",         "",        NULL}
    },
    {
        {ROUTE_BLUETOOTH_SCO,  "alsa.mixer.playback.bluetooth.sco",  "Bluetooth",         NULL},
        {ROUTE_BLUETOOTH_SCO,  "alsa.mixer.capture.bluetooth.sco",   "Bluetooth Capture", NULL}
    },
    {
        {ROUTE_HEADSET,        "alsa.mixer.playback.headset",        "Headphone", NULL},
        {ROUTE_HEADSET,        "alsa.mixer.capture.headset",         "Capture",   NULL}
    },
    {
        {ROUTE_BLUETOOTH_A2DP, "alsa.mixer.playback.bluetooth.a2dp", "Bluetooth A2DP",         NULL},
        {ROUTE_BLUETOOTH_A2DP, "alsa.mixer.capture.bluetooth.a2dp",  "Bluetooth A2DP Capture", NULL}
    },
    {
        {static_cast<audio_routes>(0), NULL, NULL, NULL},
        {static_cast<audio_routes>(0), NULL, NULL, NULL}
    }
};

// ----------------------------------------------------------------------------

AudioHardwareALSA::AudioHardwareALSA() :
    mOutput(0),
    mInput(0)
#if defined SEC_IPC
    ,mIPC(0) //for IPC
#endif
#if defined TURN_ON_DEVICE_ONLY_USE
    ,mActivatedInputDevice(false)
#endif
#if defined SYNCHRONIZE_CP	
	,mActivatedCP(false)
#endif

{
    snd_lib_error_set_handler(&ALSAErrorHandler);
    mMixer = new ALSAMixer;
#if defined SEC_IPC
    mIPC = new AudioHardwareIPC; //  IPC init
#endif
}

AudioHardwareALSA::~AudioHardwareALSA()
{
    if (mOutput) delete mOutput;
    if (mInput) delete mInput;
    if (mMixer) delete mMixer;
#if defined SEC_IPC
    if (mIPC) delete mIPC; //  for IPC
#endif
}

status_t AudioHardwareALSA::initCheck()
{
    if (mMixer && mMixer->isValid())
        return NO_ERROR;
    else
        return NO_INIT;
}

status_t AudioHardwareALSA::standby()
{
    if (mOutput)
        return mOutput->standby();

    return NO_ERROR;
}

status_t AudioHardwareALSA::setVoiceVolume(float volume)
{
	LOGI("### setVoiceVolume");
#if defined SEC_IPC
    // sangsu fix : transmic volume level IPC to modem
	if (AudioSystem::MODE_IN_CALL == mMode)
	{
	        uint32_t routes = mRoutes[mMode];

			LOGI("### route(%d) call volume(%f)", routes, volume);
	        switch (routes){
			case AudioSystem::ROUTE_EARPIECE:
			case AudioSystem::ROUTE_HEADPHONE:	// Use receive path with 3 pole headset.
				LOGI("### earpiece call volume");
				mIPC->transmitVolumeIPC(OEM_SOUND_TYPE_VOICE, volume);
				break;
			
			case AudioSystem::ROUTE_SPEAKER:			
				LOGI("### speaker call volume");
				mIPC->transmitVolumeIPC(OEM_SOUND_TYPE_SPEAKER, volume);
				break;
			
			case AudioSystem::ROUTE_BLUETOOTH_SCO:
			case AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_HEADSET:
			case AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_CARKIT:
			case AudioSystem::ROUTE_BLUETOOTH_A2DP:
				LOGI("### bluetooth call volume");
				mIPC->transmitVolumeIPC(OEM_SOUND_TYPE_BTVOICE, volume);
				break;
			
			case AudioSystem::ROUTE_HEADSET:
				LOGI("### headset call volume");
				mIPC->transmitVolumeIPC(OEM_SOUND_TYPE_HEADSET, volume);
				break;
			
			default:
				LOGE("### Call volume setting error!!!0x%08x \n", routes);
				break;	
	        }
    	}
        // sangsu fix end
#endif

    // The voice volume is used by the VOICE_CALL audio stream.
    if (mMixer)
        return mMixer->setVolume(ROUTE_EARPIECE, volume);
    else
        return INVALID_OPERATION;
}

status_t AudioHardwareALSA::setMasterVolume(float volume)
{
    if (mMixer)
        return mMixer->setMasterVolume(volume);
    else
        return INVALID_OPERATION;
}

#if defined TURN_ON_DEVICE_ONLY_USE
int AudioHardwareALSA::setMicStatus(int on)
{
        LOGI("[%s], on=%d", __func__, on);
        ALSAControl *mALSAControl = new ALSAControl();
        status_t ret =  mALSAControl->set("Mic Status", on);
        delete mALSAControl;
        return NO_ERROR;
}
#endif


#if 0
AudioStreamOut *
AudioHardwareALSA::AudioStreamOut* openOutputStream(
                int format=0,
                int channelCount=0,
                uint32_t sampleRate=0,
                status_t *status=0)
#else
AudioStreamOut *
AudioHardwareALSA::openOutputStream(
                                uint32_t devices,
                                int *format,
                                uint32_t *channels,
                                uint32_t *sampleRate,
                                status_t *status)
#endif
{
    AutoMutex lock(mLock);

    // only one output stream allowed
    if (mOutput) {
        *status = ALREADY_EXISTS;
        return 0;
    }

    LOGV("[[[[[[[[\n%s - format = %d, channels = %d, sampleRate = %d, devices = %d]]]]]]]]\n", __func__, *format, *channels, *sampleRate,devices);

    AudioStreamOutALSA *out = new AudioStreamOutALSA(this);

    *status = out->set(format, channels, sampleRate);

#ifdef PCM_OUTPUT_DUMP
		if(fpOutput == NULL)
		{
			fpOutput = fopen(PCM_OUTPUT_DUMP_PATH, "w");
			if (fpOutput == NULL)
				LOGE("fpOutput File Open Error!!"); 
		}
#endif

    if (*status == NO_ERROR) {
        mOutput = out;
        // Some information is expected to be available immediately after
        // the device is open.
        /* Tushar - Sets the current device output here - we may set device here */
        //uint32_t routes = mRoutes[mMode];
        //mOutput->setDevice(mMode, routes);
    	LOGI("%s] Setting ALSA device.", __func__);
        mOutput->setDevice(mMode, devices, PLAYBACK); /* tushar - Enable all devices as of now */
    }
    else {
        delete out;
    }

    return mOutput;
}

void 
AudioHardwareALSA::closeOutputStream(AudioStreamOut* out)
{
	/* TODO:Tushar: May lead to segmentation fault - check*/
	//delete out;
    AutoMutex lock(mLock);

#ifdef PCM_OUTPUT_DUMP
		fclose(fpOutput);
		fpOutput = NULL;
#endif

    if (mOutput == 0 || mOutput != out) {
        LOGW("Attempt to close invalid output stream");
    }
    else {
        delete mOutput;
        mOutput = 0;
    }

}

#if 0
AudioStreamIn *
AudioHardwareALSA::openInputStream(int      format,
                                   int      channelCount,
                                   uint32_t sampleRate,
                                   status_t *status,
                                   AudioSystem::audio_in_acoustics acoustics)
#else
AudioStreamIn* 
AudioHardwareALSA::openInputStream(
                                uint32_t devices,
                                int *format,
                                uint32_t *channels,
                                uint32_t *sampleRate,
                                status_t *status,
                                AudioSystem::audio_in_acoustics acoustics)
#endif
{
    AutoMutex lock(mLock);

    // only one input stream allowed
    if (mInput) {
        *status = ALREADY_EXISTS;
        return 0;
    }

    AudioStreamInALSA *in = new AudioStreamInALSA(this);

    *status = in->set(format, channels, sampleRate);
    if (*status == NO_ERROR) {
        mInput = in;
        // Some information is expected to be available immediately after
        // the device is open.
        //uint32_t routes = mRoutes[mMode];
        //mInput->setDevice(mMode, routes); 
        mInput->setDevice(mMode, devices, CAPTURE);  /* Tushar - as per modified arch */
#if defined TURN_ON_DEVICE_ONLY_USE
        mActivatedInputDevice = true;
        setMicStatus(1);
		
#ifdef PCM_INPUT_DUMP
		if(fpInput == NULL)
		{
			fpInput = fopen(PCM_INPUT_DUMP_PATH, "w");
			if (fpInput == NULL)
				LOGE("fpInput File Open Error!!"); 
		}
#endif

#endif
	return mInput;
    }
    else {
        delete in;
    }
    return mInput;
}

void
AudioHardwareALSA::closeInputStream(AudioStreamIn* in)
{
	/* TODO:Tushar: May lead to segmentation fault - check*/
	//delete in;
	AutoMutex lock(mLock);

    if (mInput == 0 || mInput != in) {
        LOGW("Attempt to close invalid input stream");
    }
    else {
        delete mInput;
        mInput = 0;
#ifdef PCM_INPUT_DUMP
	fclose(fpInput);
	fpInput = NULL;
#endif
#if defined TURN_ON_DEVICE_ONLY_USE
        mActivatedInputDevice = false;
        setMicStatus(0);
#endif

    }
}

#if defined SEC_SWP_SOUND
status_t AudioHardwareALSA::doRouting(uint32_t device)
{
	uint32_t routes;
	status_t ret;

	AutoMutex lock(mLock);
	int mode = mMode;		// Prevent to changing mode on setup sequence.

	if (mOutput) {
		routes = device;
		//routes = 0; /* Tushar - temp implementation */

		// Setup sound path for CP clocking

#if defined SEC_IPC
		
		if (AudioSystem::MODE_IN_CALL == mode)
		{ 

					LOGI("### incall mode route (%d)", routes);
			switch(routes){
				case AudioSystem::ROUTE_EARPIECE:
					LOGI("### incall mode earpiece route");
					mIPC->transmitAudioPathIPC(OEM_SOUND_AUDIO_PATH_HANDSET);
					break;
				
				case AudioSystem::ROUTE_SPEAKER:
					LOGI("### incall mode speaker route");
					mIPC->transmitAudioPathIPC(OEM_SOUND_AUDIO_PATH_SPEAKER);
					break;
				
				case AudioSystem::ROUTE_BLUETOOTH_SCO:
				case AudioSystem::ROUTE_BLUETOOTH_SCO_HEADSET:
				case AudioSystem::ROUTE_BLUETOOTH_SCO_CARKIT:
#if defined BT_NR_EC_ONOFF
					if(mBluetoothECOff)
					{
						LOGI("### incall mode bluetooth EC OFF route");
						mIPC->transmitAudioPathIPC(OEM_SOUND_AUDIO_PATH_BT_NSEC_OFF);						
					}
					else
					{
#endif
						LOGI("### incall mode bluetooth route");
						mIPC->transmitAudioPathIPC(OEM_SOUND_AUDIO_PATH_BLUETOOTH);
#if defined BT_NR_EC_ONOFF
					}
#endif
					break;

				case AudioSystem::ROUTE_HEADSET :
				case AudioSystem::ROUTE_HEADPHONE :
					LOGI("### incall mode headset route");
					mIPC->transmitAudioPathIPC(OEM_SOUND_AUDIO_PATH_HEADSET);
					break;

				case AudioSystem::ROUTE_BLUETOOTH_A2DP:
					LOGI("### incall mode bluetooth route");
					mIPC->transmitAudioPathIPC(OEM_SOUND_AUDIO_PATH_BLUETOOTH);
					break;

				default:
					LOGE("### incall mode Error!! route = [%d]", routes);
					break;
			}
		}
#endif// end of #if defined SEC_IPC

	ret = mOutput->setDevice(mode, routes, PLAYBACK);

#if defined SEC_IPC
		if (AudioSystem::MODE_IN_CALL == mode)
		{ 
#if defined SYNCHRONIZE_CP	
			if(!mActivatedCP)
			{
				mIPC->transmitClock_IPC(OEM_SOUND_CLOCK_START);
				mActivatedCP = true;
			}
#endif
	        }

		if (AudioSystem::MODE_NORMAL== mode)		// Call stop.
		{
#if defined SYNCHRONIZE_CP			
			if(mActivatedCP)
				mActivatedCP = false;
#endif

		}
#endif	// end of #if defined SEC_IPC

#ifndef SYNCHRONIZE_CP	
		ret = mOutput->setDevice(mode, routes, PLAYBACK);
#endif
		return ret;
	}

	return NO_INIT;
}

#else
/** This function is no more used */
status_t AudioHardwareALSA::doRouting()
{
    uint32_t routes;
    AutoMutex lock(mLock);

    LOGD("Inside AudioHardwareALSA::doRouting \n");
    if (mOutput) {
        //routes = mRoutes[mMode];
        routes = 0; /* Tushar - temp implementation */
        return mOutput->setDevice(mMode, routes, PLAYBACK);
    }
    return NO_INIT;

}
#endif

status_t AudioHardwareALSA::setMicMute(bool state)
{
    if (mMixer)
        return mMixer->setCaptureMuteState(ROUTE_EARPIECE, state);

    return NO_INIT;
}

status_t AudioHardwareALSA::getMicMute(bool *state)
{
    if (mMixer)
        return mMixer->getCaptureMuteState(ROUTE_EARPIECE, state);

    return NO_ERROR;
}

status_t AudioHardwareALSA::dump(int fd, const Vector<String16>& args)
{
    return NO_ERROR;
}


size_t AudioHardwareALSA::getInputBufferSize(uint32_t sampleRate, int format, int channelCount)
{
    if (sampleRate != 8000  && sampleRate != 11025 && sampleRate != 16000 && sampleRate != 22050 &&
        sampleRate != 24000 && sampleRate != 32000 && sampleRate != 44100 && sampleRate != 48000) {
        LOGW("getInputBufferSize bad sampling rate: %d", sampleRate);
        return 0;
    }
    if (format != AudioSystem::PCM_16_BIT) {
        LOGW("getInputBufferSize bad format: %d", format);
        return 0;
    }
    if (channelCount != 1) {
        LOGW("getInputBufferSize bad channel count: %d", channelCount);
        return 0;
    }

#if defined SEC_SWP_SOUND
    if (sampleRate == 32000 || sampleRate == 44100 || sampleRate == 48000)
        return READ_FRAME_SIZE_STANDARD;
    else
        return READ_FRAME_SIZE;
#else /* SEC_SWP_SOUND */
    return 320;
#endif /* SEC_SWP_SOUND */
}

// ----------------------------------------------------------------------------

ALSAStreamOps::ALSAStreamOps() :
    mHandle(0),
    mHardwareParams(0),
    mSoftwareParams(0),
    mMode(-1),
    mDevice(0)
{
    if (snd_pcm_hw_params_malloc(&mHardwareParams) < 0) {
        LOG_ALWAYS_FATAL("Failed to allocate ALSA hardware parameters!");
    }

    if (snd_pcm_sw_params_malloc(&mSoftwareParams) < 0) {
        LOG_ALWAYS_FATAL("Failed to allocate ALSA software parameters!");
    }
}

ALSAStreamOps::~ALSAStreamOps()
{
    AutoMutex lock(mLock);

    close();

    if (mHardwareParams)
        snd_pcm_hw_params_free(mHardwareParams);

    if (mSoftwareParams)
        snd_pcm_sw_params_free(mSoftwareParams);
}


status_t ALSAStreamOps::set(int      *pformat,
                            uint32_t *pchannels,
                            uint32_t *prate)
{
	int lformat = pformat ? *pformat : 0;
	unsigned int lchannels = pchannels ? *pchannels : 0;
	unsigned int lrate = prate ? *prate : 0;


    	LOGD("ALSAStreamOps - input   - format = %d, channels = %d, rate = %d\n", lformat, lchannels, lrate);
    	LOGD("ALSAStreamOps - default - format = %d, channels = %d, rate = %d\n", mDefaults->format, mDefaults->channels, mDefaults->sampleRate);

	if(lformat == 0) lformat = getAndroidFormat(mDefaults->format);//format();
	if(lchannels == 0) lchannels = getAndroidChannels(mDefaults->channels);// channelCount();
	if(lrate == 0) lrate = mDefaults->sampleRate; 

	if((lformat != getAndroidFormat(mDefaults->format)) ||
	 	(lchannels != getAndroidChannels(mDefaults->channels)) ||
		(lrate != mDefaults->sampleRate)){
		if(pformat)     *pformat = getAndroidFormat(mDefaults->format);
		if(pchannels)   *pchannels = getAndroidChannels(mDefaults->channels);
		if(prate)       *prate = mDefaults->sampleRate;
		return BAD_VALUE;
	} 

	if(pformat)     *pformat = getAndroidFormat(mDefaults->format);
	if(pchannels)   *pchannels = getAndroidChannels(mDefaults->channels);
	if(prate)       *prate = mDefaults->sampleRate;

	return NO_ERROR;
}


uint32_t ALSAStreamOps::sampleRate() const
{
    unsigned int rate;
    int err;

    if (! mHandle)
        return NO_INIT;

    return snd_pcm_hw_params_get_rate(mHardwareParams, &rate, 0) < 0
        ? 0 : static_cast<uint32_t>(rate);
}

status_t ALSAStreamOps::sampleRate(uint32_t rate)
{
    const char *stream;
    unsigned int requestedRate;
    int err;

    if (!mHandle)
        return NO_INIT;

    stream = streamName();
    requestedRate = rate;
    err = snd_pcm_hw_params_set_rate_near(mHandle,
                                          mHardwareParams,
                                          &requestedRate,
                                          0);

    if (err < 0) {
        LOGE("Unable to set %s sample rate to %u: %s",
            stream, rate, snd_strerror(err));
        return BAD_VALUE;
    }
    if (requestedRate != rate) {
        // Some devices have a fixed sample rate, and can not be changed.
        // This may cause resampling problems; i.e. PCM playback will be too
        // slow or fast.
        LOGW("Requested rate (%u HZ) does not match actual rate (%u HZ)",
            rate, requestedRate);
    }
    else {
        LOGD("Set %s sample rate to %u HZ", stream, requestedRate);
    }
    return NO_ERROR;
}

//
// Return the number of bytes (not frames)
//
size_t ALSAStreamOps::bufferSize() const
{
    int err;

    if (!mHandle)
        return -1;

    snd_pcm_uframes_t bufferSize = 0;
    snd_pcm_uframes_t periodSize = 0;

    err = snd_pcm_get_params(mHandle, &bufferSize, &periodSize);

    if (err < 0)
        return -1;

    return static_cast<size_t>(snd_pcm_frames_to_bytes(mHandle, bufferSize));
}

int ALSAStreamOps::getAndroidFormat(snd_pcm_format_t format)
{
    int pcmFormatBitWidth;
    int audioSystemFormat;

    pcmFormatBitWidth = snd_pcm_format_physical_width(format);
    audioSystemFormat = AudioSystem::DEFAULT;
    switch(pcmFormatBitWidth) {
        case 8:
            audioSystemFormat = AudioSystem::PCM_8_BIT;
            break;

        case 16:
            audioSystemFormat = AudioSystem::PCM_16_BIT;
            break;

        default:
            LOG_FATAL("Unknown AudioSystem bit width %i!", pcmFormatBitWidth);
    }

    return audioSystemFormat;

}

int ALSAStreamOps::format() const
{
    snd_pcm_format_t ALSAFormat;
    int pcmFormatBitWidth;
    int audioSystemFormat;

    if (!mHandle)
        return -1;

    if (snd_pcm_hw_params_get_format(mHardwareParams, &ALSAFormat) < 0) {
        return -1;
    }

    pcmFormatBitWidth = snd_pcm_format_physical_width(ALSAFormat);
    audioSystemFormat = AudioSystem::DEFAULT;
    switch(pcmFormatBitWidth) {
        case 8:
            audioSystemFormat = AudioSystem::PCM_8_BIT;
            break;

        case 16:
            audioSystemFormat = AudioSystem::PCM_16_BIT;
            break;

        default:
            LOG_FATAL("Unknown AudioSystem bit width %i!", pcmFormatBitWidth);
    }

    return audioSystemFormat;
}

uint32_t ALSAStreamOps::getAndroidChannels(int channels)
{
    int AudioSystemChannels = AudioSystem::DEFAULT;

    switch(channels){
	case 1:
		AudioSystemChannels = AudioSystem::CHANNEL_OUT_FRONT_RIGHT;
		break;
	case 2:
		AudioSystemChannels = AudioSystem::CHANNEL_OUT_STEREO;
		break;
	case 4:
		AudioSystemChannels = AudioSystem::CHANNEL_OUT_QUAD;
		break;
	case 6:
		AudioSystemChannels = AudioSystem::CHANNEL_OUT_5POINT1;
		break;
	defualt:
		LOGE("FATAL: AudioSystem does not support %d channels.", channels);		
	}
    return AudioSystemChannels;
}

int ALSAStreamOps::channelCount() const
{
    unsigned int val;
    int err;

    int AudioSystemChannels;

    if (!mHandle)
        return -1;

    err = snd_pcm_hw_params_get_channels(mHardwareParams, &val);
    if (err < 0) {
        LOGE("Unable to get device channel count: %s",
            snd_strerror(err));
        return -1;
    }

    AudioSystemChannels = AudioSystem::DEFAULT;

    switch(val){
	case 1:
		AudioSystemChannels = AudioSystem::CHANNEL_OUT_FRONT_RIGHT;
		break;
	case 2:
		AudioSystemChannels = AudioSystem::CHANNEL_OUT_STEREO;
		break;
	case 4:
		AudioSystemChannels = AudioSystem::CHANNEL_OUT_QUAD;
		break;
	case 6:
		AudioSystemChannels = AudioSystem::CHANNEL_OUT_5POINT1;
		break;
	defualt:
		LOGE("FATAL: AudioSystem does not support %d channels.", val);		
	}


    return AudioSystemChannels;
}

status_t ALSAStreamOps::channelCount(int channels) {
    int err;

    if (!mHandle)
        return NO_INIT;

   // if(channels == 1) channels = 2; //Kamat: This is a fix added to avoid audioflinger crash (current audio driver does not support mono). Please check and modify suitably later.

    err = snd_pcm_hw_params_set_channels(mHandle, mHardwareParams, channels);
    if (err < 0) {
        LOGE("Unable to set channel count to %i: %s",
            channels, snd_strerror(err));
        return BAD_VALUE;
    }

    LOGD("Using %i %s for %s.",
        channels, channels == 1 ? "channel" : "channels", streamName());

    return NO_ERROR;
}

status_t ALSAStreamOps::open(int mode, uint32_t device)
{
    const char *stream = streamName();
    const char *devName = deviceName(mode, device);

    int         err;

	LOGI("Try to open ALSA %s device %s", stream, devName);

    for(;;) {
        // The PCM stream is opened in blocking mode, per ALSA defaults.  The
        // AudioFlinger seems to assume blocking mode too, so asynchronous mode
        // should not be used.
        err = snd_pcm_open(&mHandle, devName, mDefaults->direction, 0);
        if (err == 0) break;

        // See if there is a less specific name we can try.
        // Note: We are changing the contents of a const char * here.
        char *tail = strrchr(devName, '_');
        if (! tail) break;
        *tail = 0;
    }

    if (err < 0) {
        // None of the Android defined audio devices exist. Open a generic one.
		devName = "hw:00,1";	// 090507 SMDKC110 Froyo

        err = snd_pcm_open(&mHandle, devName, mDefaults->direction, 0);
        if (err < 0) {
            // Last resort is the NULL device (i.e. the bit bucket).
            devName = _nullALSADeviceName;
            err = snd_pcm_open(&mHandle, devName, mDefaults->direction, 0);
        }
    }

    mMode   = mode;
    mDevice = device;

    LOGI("Initialized ALSA %s device %s", stream, devName);
    return err;
}

void ALSAStreamOps::close()
{
    snd_pcm_t *handle = mHandle;
    mHandle = NULL;

    if (handle) {
        snd_pcm_close(handle);
        mMode   = -1;
        mDevice = 0;
    }
}

status_t ALSAStreamOps::setSoftwareParams()
{
    if (!mHandle)
        return NO_INIT;

    int err;

    // Get the current software parameters
    err = snd_pcm_sw_params_current(mHandle, mSoftwareParams);
    if (err < 0) {
        LOGE("Unable to get software parameters: %s", snd_strerror(err));
        return NO_INIT;
    }

    snd_pcm_uframes_t bufferSize = 0;
    snd_pcm_uframes_t periodSize = 0;
    snd_pcm_uframes_t startThreshold;

    // Configure ALSA to start the transfer when the buffer is almost full.
    snd_pcm_get_params(mHandle, &bufferSize, &periodSize);
    LOGE("bufferSize %d, periodSize %d\n", bufferSize, periodSize);

    if (mDefaults->direction == SND_PCM_STREAM_PLAYBACK) {
        // For playback, configure ALSA to start the transfer when the
        // buffer is almost full.
        startThreshold = (bufferSize / periodSize) * periodSize;
        //startThreshold = 1;
    }
    else {
        // For recording, configure ALSA to start the transfer on the
        // first frame.
        startThreshold = 1;
    }

    err = snd_pcm_sw_params_set_start_threshold(mHandle,
        mSoftwareParams,
        startThreshold);
    if (err < 0) {
        LOGE("Unable to set start threshold to %lu frames: %s",
            startThreshold, snd_strerror(err));
        return NO_INIT;
    }

    // Stop the transfer when the buffer is full.
    err = snd_pcm_sw_params_set_stop_threshold(mHandle,
                                               mSoftwareParams,
                                               bufferSize);
    if (err < 0) {
        LOGE("Unable to set stop threshold to %lu frames: %s",
            bufferSize, snd_strerror(err));
        return NO_INIT;
    }

    // Allow the transfer to start when at least periodSize samples can be
    // processed.
    err = snd_pcm_sw_params_set_avail_min(mHandle,
                                          mSoftwareParams,
                                          periodSize);
    if (err < 0) {
        LOGE("Unable to configure available minimum to %lu: %s",
            periodSize, snd_strerror(err));
        return NO_INIT;
    }

    // Commit the software parameters back to the device.
    err = snd_pcm_sw_params(mHandle, mSoftwareParams);
    if (err < 0) {
        LOGE("Unable to configure software parameters: %s",
            snd_strerror(err));
        return NO_INIT;
    }

    return NO_ERROR;
}

status_t ALSAStreamOps::setPCMFormat(snd_pcm_format_t format)
{
    const char *formatDesc;
    const char *formatName;
    bool validFormat;
    int err;

    // snd_pcm_format_description() and snd_pcm_format_name() do not perform
    // proper bounds checking.
    validFormat = (static_cast<int>(format) > SND_PCM_FORMAT_UNKNOWN) &&
        (static_cast<int>(format) <= SND_PCM_FORMAT_LAST);
    formatDesc = validFormat ?
        snd_pcm_format_description(format) : "Invalid Format";
    formatName = validFormat ?
        snd_pcm_format_name(format) : "UNKNOWN";

    err = snd_pcm_hw_params_set_format(mHandle, mHardwareParams, format);
    if (err < 0) {
        LOGE("Unable to configure PCM format %s (%s): %s",
            formatName, formatDesc, snd_strerror(err));
        return NO_INIT;
    }

    LOGD("Set %s PCM format to %s (%s)", streamName(), formatName, formatDesc);
    return NO_ERROR;
}

status_t ALSAStreamOps::setHardwareResample(bool resample)
{
    int err;

    err = snd_pcm_hw_params_set_rate_resample(mHandle,
                                              mHardwareParams,
                                              static_cast<int>(resample));
    if (err < 0) {
        LOGE("Unable to %s hardware resampling: %s",
            resample ? "enable" : "disable",
            snd_strerror(err));
        return NO_INIT;
    }
    return NO_ERROR;
}

const char *ALSAStreamOps::streamName()
{
    // Don't use snd_pcm_stream(mHandle), as the PCM stream may not be
    // opened yet.  In such case, snd_pcm_stream() will abort().
    return snd_pcm_stream_name(mDefaults->direction);
}

//
// Set playback or capture PCM device.  It's possible to support audio output
// or input from multiple devices by using the ALSA plugins, but this is
// not supported for simplicity.
//
// The AudioHardwareALSA API does not allow one to set the input routing.
//
// If the "routes" value does not map to a valid device, the default playback
// device is used.
//
status_t ALSAStreamOps::setDevice(int mode, uint32_t device, uint audio_mode)
{
    // Close off previously opened device.
    // It would be nice to determine if the underlying device actually
    // changes, but we might be manipulating mixer settings (see asound.conf).
    //
    close();

    const char *stream = streamName();


    LOGD("\n------------------------>>>>>> ALSA OPEN mode %d,device %d \n",mode,device);

    status_t    status = open (mode, device);
    int     err;
    unsigned int period_val;

    if (status != NO_ERROR)
        return status;

    err = snd_pcm_hw_params_any(mHandle, mHardwareParams);
    if (err < 0) {
        LOGE("Unable to configure hardware: %s", snd_strerror(err));
        return NO_INIT;
    }

    status = setPCMFormat(mDefaults->format);

    // Set the interleaved read and write format.
    err = snd_pcm_hw_params_set_access(mHandle, mHardwareParams,
                                       SND_PCM_ACCESS_RW_INTERLEAVED);
    if (err < 0) {
        LOGE("Unable to configure PCM read/write format: %s",
            snd_strerror(err));
        return NO_INIT;
    }

    //
    // Some devices do not have the default two channels.  Force an error to
    // prevent AudioMixer from crashing and taking the whole system down.
    //
    // Note that some devices will return an -EINVAL if the channel count
    // is queried before it has been set.  i.e. calling channelCount()
    // before channelCount(channels) may return -EINVAL.
    //
    status = channelCount(mDefaults->channels);
    if (status != NO_ERROR)
        return status;

    // Don't check for failure; some devices do not support the default
    // sample rate.

    sampleRate(mDefaults->sampleRate);

    // Disable hardware resampling.
    status = setHardwareResample(false);
    if (status != NO_ERROR)
        return status;

    snd_pcm_uframes_t bufferSize = mDefaults->bufferSize;

    unsigned int latency = mDefaults->latency;

    // Make sure we have at least the size we originally wanted
    err = snd_pcm_hw_params_set_buffer_size(mHandle, mHardwareParams, bufferSize);
    if (err < 0) {
        LOGE("Unable to set buffer size to %d:  %s",
             (int)bufferSize, snd_strerror(err));
        return NO_INIT;
    }

    // Setup buffers for latency
    err = snd_pcm_hw_params_set_buffer_time_near (mHandle, mHardwareParams,
                                                  &latency, NULL);
    if(audio_mode == PLAYBACK) {
		period_val = PERIOD_PLAYBACK;
		if(snd_pcm_hw_params_set_periods(mHandle, mHardwareParams, period_val, 0) < 0) 
	    	LOGE("Fail to set period size %d for playback", period_val);
    }
    else
		period_val = PERIOD_CAPTURE;

    if (err < 0) {
        /* That didn't work, set the period instead */
        unsigned int periodTime = latency / period_val;
        err = snd_pcm_hw_params_set_period_time_near (mHandle, mHardwareParams,
                                                      &periodTime, NULL);
        if (err < 0) {
            LOGE("Unable to set the period time for latency: %s", snd_strerror(err));
            return NO_INIT;
        }
        snd_pcm_uframes_t periodSize;
        err = snd_pcm_hw_params_get_period_size (mHardwareParams, &periodSize, NULL);
        if (err < 0) {
            LOGE("Unable to get the period size for latency: %s", snd_strerror(err));
            return NO_INIT;
        }
        bufferSize = periodSize * period_val;
        if (bufferSize < mDefaults->bufferSize)
            bufferSize = mDefaults->bufferSize;
        err = snd_pcm_hw_params_set_buffer_size_near (mHandle, mHardwareParams, &bufferSize);
        if (err < 0) {
            LOGE("Unable to set the buffer size for latency: %s", snd_strerror(err));
            return NO_INIT;
        }
    } else {
        // OK, we got buffer time near what we expect. See what that did for bufferSize.
        err = snd_pcm_hw_params_get_buffer_size (mHardwareParams, &bufferSize);
        if (err < 0) {
            LOGE("Unable to get the buffer size for latency: %s", snd_strerror(err));
            return NO_INIT;
        }
        // Does set_buffer_time_near change the passed value? It should.
        err = snd_pcm_hw_params_get_buffer_time (mHardwareParams, &latency, NULL);
        if (err < 0) {
            LOGE("Unable to get the buffer time for latency: %s", snd_strerror(err));
            return NO_INIT;
        }
        unsigned int periodTime = latency / period_val;
        err = snd_pcm_hw_params_set_period_time_near (mHandle, mHardwareParams,
                                                      &periodTime, NULL);
        if (err < 0) {
            LOGE("Unable to set the period time for latency: %s", snd_strerror(err));
            return NO_INIT;
        }
    }

    LOGD("Buffer size: %d", (int)bufferSize);
    LOGD("Latency: %d", (int)latency);

    mDefaults->bufferSize = bufferSize;
    mDefaults->latency = latency;

    // Commit the hardware parameters back to the device.
    err = snd_pcm_hw_params(mHandle, mHardwareParams);
    if (err < 0) {
        LOGE("Unable to set hardware parameters: %s", snd_strerror(err));
        return NO_INIT;
    }

    status = setSoftwareParams();

    return status;
}

const char *ALSAStreamOps::deviceName(int mode, uint32_t device)
{
    static char devString[ALSA_NAME_MAX];
    int dev;
    int hasDevExt = 0;

    strcpy (devString, mDefaults->devicePrefix);

    for (dev=0; device; dev++)
        if (device & (1 << dev)) {
            /* Don't go past the end of our list */
            if (dev >= deviceSuffixLen)
                break;
            ALSA_STRCAT (devString, deviceSuffix[dev]);
            device &= ~(1 << dev);
            hasDevExt = 1;
        }

    if (hasDevExt)
        switch (mode) {
            case AudioSystem::MODE_NORMAL:
                ALSA_STRCAT (devString, "_normal");
                break;
            case AudioSystem::MODE_RINGTONE:
                ALSA_STRCAT (devString, "_ringtone");
                break;
            case AudioSystem::MODE_IN_CALL:
                ALSA_STRCAT (devString, "_incall");
                break;
        };

    return devString;
}

// ----------------------------------------------------------------------------

AudioStreamOutALSA::AudioStreamOutALSA(AudioHardwareALSA *parent) :
    mParent(parent),
    mPowerLock(false)
{
    static StreamDefaults _defaults = {
        devicePrefix   : "AndroidPlayback",
        direction      : SND_PCM_STREAM_PLAYBACK,
        format         : SND_PCM_FORMAT_S16_LE,   // AudioSystem::PCM_16_BIT
        channels       : 2,
        sampleRate     : DEFAULT_SAMPLE_RATE,
        latency        : 250000,                  // Desired Delay in usec
	bufferSize     : 4096,                   // Desired Number of samples
};

    setStreamDefaults(&_defaults);
}

AudioStreamOutALSA::~AudioStreamOutALSA()
{
    standby();
    mParent->mOutput = NULL;
}

//int AudioStreamOutALSA::channelCount() const
uint32_t AudioStreamOutALSA::channels() const
{
    uint32_t c = ALSAStreamOps::channelCount();

    // AudioMixer will seg fault if it doesn't have two channels.
    LOGW_IF(c != AudioSystem::CHANNEL_OUT_STEREO,
        "AudioMixer expects two channels, but only %i found!", c);
    return c;
}

/* New arch */
status_t AudioStreamOutALSA::setVolume(float left, float right)
{
    if (! mParent->mMixer || ! mDevice)
        return NO_INIT;

	/** Tushar - Need to decide on the volume value 
	 * that we pass onto the mixer. */
    return mParent->mMixer->setVolume (mDevice, (left + right)/2);
}

status_t AudioStreamOutALSA::setVolume(float volume)
{
    if (! mParent->mMixer || ! mDevice)
        return NO_INIT;

    return mParent->mMixer->setVolume (mDevice, volume);
}

/* New Arch */
status_t    AudioStreamOutALSA::setParameters(const String8& keyValuePairs)
{
#if defined SLSI_S5PC110
    AudioParameter param = AudioParameter(keyValuePairs);
    status_t status = NO_ERROR;
    int device;
    int value;
    LOGD("AudioStreamOutALSA::setParameters() %s", keyValuePairs.string());
    
    if (param.getInt(String8(AudioParameter::keyRouting), device) == NO_ERROR) 
    {
        mDevice = device;

	if (mParent->mInput) mParent->mInput->mDevice = device;
	mParent->mRoutes[mParent->mMode] = mDevice;
        mParent->doRouting(mDevice);

        param.remove(String8(AudioParameter::keyRouting));
    }
    else if (param.getInt(String8(AudioParameter::keySamplingRate), value) == NO_ERROR)
    {
		mParent->mOutput->mDefaults->sampleRate = value;
		mParent->doRouting(mDevice);
		param.remove(String8(AudioParameter::keySamplingRate));
    }
	
    if (param.size()) {
        status = BAD_VALUE;
    }
    return status;
#else
	/* TODO: Implement as per new arch */

        LOGD("AudioStreamOutAlsa::setParameters... %s \n\n",keyValuePairs.string());
    if (! mParent->mOutput )//|| ! mMode)
	return NO_INIT;


	int device = keyValuePairs.string()[keyValuePairs.length()-1] - 48 -1 ; //easy conversion frm ascii to int and then to required number
        LOGV("\n\n-------->> ALSA SET PARAMS  device %d \n\n",(1<<device));
	mParent->mOutput->setDevice(mMode, 1<<device, PLAYBACK);
	return NO_ERROR; 
#endif
}
String8  AudioStreamOutALSA::getParameters(const String8& keys)
{
#if defined SLSI_S5PC110
    AudioParameter param = AudioParameter(keys);
    String8 value;
    String8 key = String8(AudioParameter::keyRouting);
    
    if (param.get(key, value) == NO_ERROR) {
        param.addInt(key, (int)mDevice);
    }
    
    LOGD("AudioStreamOutALSA::getParameters() %s", param.toString().string());
    return param.toString();
#else
	/* TODO: Implement as per new arch */
	return keys;	
#endif
}

status_t  AudioStreamOutALSA::getRenderPosition(uint32_t *dspFrames)
{

	//TODO: enable when supported by driver
	return INVALID_OPERATION;
}

#if 1 // Fix for underrun error
ssize_t AudioStreamOutALSA::write(const void *buffer, size_t bytes)
{
    snd_pcm_sframes_t n;
    size_t            sent = 0;
    status_t          err;

    AutoMutex lock(mLock);

    if (!mPowerLock) {
	ALSAStreamOps::setDevice(mMode, mDevice, PLAYBACK);
        acquire_wake_lock (PARTIAL_WAKE_LOCK, "AudioOutLock");
        mPowerLock = true;
    }
   // if (isStandby())
   //     return 0;

#ifdef PCM_OUTPUT_DUMP
	fwrite(buffer, bytes, 1, fpOutput);
	LOGD("Output PCM dumped!!");
#endif
    if (!mHandle){ 
		LOGD("Calling setDevice from write @..%d.\n",__LINE__);
		ALSAStreamOps::setDevice(mMode, mDevice, PLAYBACK);
      }
    do {
	// write correct number of bytes per attempt
        n = snd_pcm_writei(mHandle,
                           (char *)buffer + sent,
                           snd_pcm_bytes_to_frames(mHandle, bytes-sent));
        if (n == -EBADFD) {
		LOGD("Calling setDevice.. pcm_write returned error @..%d.\n",__LINE__);
	  	// Somehow the stream is in a bad state. The driver probably
	  	// has a bug and snd_pcm_recover() doesn't seem to handle this.
	  	ALSAStreamOps::setDevice(mMode, mDevice, PLAYBACK);
            }
        else if (n < 0) {
            if (mHandle) {
		// snd_pcm_recover() will return 0 if successful in recovering from
		//             // an error, or -errno if the error was unrecoverable.
		// We can make silent bit on as we are now handling the under-run and there will not be any data loss due to under-run
		n = snd_pcm_recover(mHandle, n, 1);	
            	if (n)
              	  return static_cast<ssize_t>(n);
            }
         }
         else
            sent += static_cast<ssize_t>(snd_pcm_frames_to_bytes(mHandle, n));
    } while (mHandle && sent < bytes);
    //LOGI("Request Bytes=%d, Actual Written=%d",bytes,sent);
    return sent;
}
#else
ssize_t AudioStreamOutALSA::write(const void *buffer, size_t bytes)
{
    snd_pcm_sframes_t n;
    status_t          err;

    AutoMutex lock(mLock);
#if 0
    if (isStandby())
        return 0;
#endif
    if (!mPowerLock) {
        acquire_wake_lock (PARTIAL_WAKE_LOCK, "AudioLock");
        ALSAStreamOps::setDevice(mMode, mDevice,PLAYBACK);
        mPowerLock = true;
    }

    n = snd_pcm_writei(mHandle,
                       buffer,
                       snd_pcm_bytes_to_frames(mHandle, bytes));
    if (n < 0 && mHandle) {
        // snd_pcm_recover() will return 0 if successful in recovering from
        // an error, or -errno if the error was unrecoverable.
	//device driver sometimes does not recover -vladi	
        n = snd_pcm_recover(mHandle, n, 0);
	if(n < 0)  //if recover fails
	ALSAStreamOps::setDevice(mMode, mDevice, PLAYBACK);
    }

    return static_cast<ssize_t>(n);
}
#endif



status_t AudioStreamOutALSA::dump(int fd, const Vector<String16>& args)
{
    return NO_ERROR;
}

status_t AudioStreamOutALSA::setDevice(int mode, uint32_t newDevice, uint32_t audio_mode)
{
    AutoMutex lock(mLock);

    return ALSAStreamOps::setDevice(mode, newDevice, audio_mode);
}

status_t AudioStreamOutALSA::standby()
{
    AutoMutex lock(mLock);
    LOGD("Inside AudioStreamOutALSA::standby\n");
    if (mHandle)
        snd_pcm_drain (mHandle);

    if (mPowerLock) {
	     if(!mParent->mActivatedInputDevice){      // Let PCM device alive on activating input stream.
		snd_pcm_close(mHandle);
		mHandle = NULL;
#if 1 // Fix for underrun error
		release_wake_lock ("AudioOutLock");
#else
		release_wake_lock ("AudioLock");
#endif
	        mPowerLock = false;
    	     }
	}
//	close(); //Don't call this as this function will reset the mode also
    return NO_ERROR;
}

bool AudioStreamOutALSA::isStandby()
{
    return (!mHandle);
}

#define USEC_TO_MSEC(x) ((x + 999) / 1000)

uint32_t AudioStreamOutALSA::latency() const
{
    // Android wants latency in milliseconds.
    return USEC_TO_MSEC (mDefaults->latency);
}

// ----------------------------------------------------------------------------

AudioStreamInALSA::AudioStreamInALSA(AudioHardwareALSA *parent) :
    mParent(parent),
    mPowerLock(false)
{
    static StreamDefaults _defaults = {
        devicePrefix   : "AndroidRecord",
        direction      : SND_PCM_STREAM_CAPTURE,
        format         : SND_PCM_FORMAT_S16_LE,   // AudioSystem::PCM_16_BIT
        channels       : 1,
        sampleRate     : AudioRecord::DEFAULT_SAMPLE_RATE,
        latency        : 250000,                  // Desired Delay in usec
        bufferSize     : 4096,                   // Desired Number of samples
        };

    setStreamDefaults(&_defaults);
}

AudioStreamInALSA::~AudioStreamInALSA()
{
    if (mPowerLock) {
	    snd_pcm_close(mHandle);
	    mHandle = NULL;
    	    release_wake_lock ("AudioInLock");
     	    mPowerLock = false;
    }
    mParent->mInput = NULL;
}

status_t AudioStreamInALSA::setGain(float gain)
{
    if (mParent->mMixer)
        return mParent->mMixer->setMasterGain (gain);
    else
        return NO_INIT;
}

ssize_t AudioStreamInALSA::read(void *buffer, ssize_t bytes)
{
    snd_pcm_sframes_t n;
    status_t          err;

    AutoMutex lock(mLock);

    if (!mPowerLock) {
        acquire_wake_lock (PARTIAL_WAKE_LOCK, "AudioInLock");
		
#ifdef PCM_INPUT_DUMP
		fwrite(buffer, readBytes, 1, fpInput);
		LOGD("Input PCM dumped!!");
#endif

		LOGD("Calling setDevice from read@..%d.\n",__LINE__);
        ALSAStreamOps::setDevice(mMode, mDevice,CAPTURE);
        mPowerLock = true;
    }
    n = snd_pcm_readi(mHandle,
                      buffer,
                      snd_pcm_bytes_to_frames(mHandle, bytes));
    if (n < 0 && mHandle) {
        n = snd_pcm_recover(mHandle, n, 0);
    }

#ifdef PCM_INPUT_DUMP
	fwrite(buffer, bytes, 1, fpInput);
	LOGD("Input PCM dumped!!");
#endif

    return static_cast<ssize_t>(n);
}

status_t AudioStreamInALSA::dump(int fd, const Vector<String16>& args)
{
    return NO_ERROR;
}

status_t AudioStreamInALSA::setDevice(int mode, uint32_t newDevice, uint32_t audio_mode)
{
    AutoMutex lock(mLock);

    return ALSAStreamOps::setDevice(mode, newDevice, audio_mode);
}

status_t AudioStreamInALSA::standby()
{
    AutoMutex lock(mLock);

    LOGD("Entering AudioStreamInALSA::standby\n");
    if (mPowerLock) {
            mParent->mActivatedInputDevice = false;
	    snd_pcm_close(mHandle);
	    mHandle = NULL;
    	    release_wake_lock ("AudioInLock");
     	    mPowerLock = false;
    }

    return NO_ERROR;
}

/* New Arch */
status_t    AudioStreamInALSA::setParameters(const String8& keyValuePairs)
{
#if defined SLSI_S5PC110
		AudioParameter param = AudioParameter(keyValuePairs);
        String8 key = String8(AudioParameter::keyRouting);
        status_t status = NO_ERROR;
        int device;
        LOGD("AudioStreamInALSA::setParameters() %s", keyValuePairs.string());
        
        if (param.getInt(key, device) == NO_ERROR) {
		mDevice = device;
		if(mDevice != 0)
			setDevice(mMode, mDevice, CAPTURE);
		param.remove(key);
        }
        
        if (param.size()) {
            status = BAD_VALUE;
        }
        return status;
#else
        /* TODO: Implement as per new arch */

    if (! mParent->mInput )//|| ! mMode)
	return NO_INIT;

	//	yman.seo use setDevice temp.
	int device = keyValuePairs.string()[keyValuePairs.length()-1] - 48 -1 ; //easy conversion frm ascii to int and then to required number
        LOGD("\n\n-------->> ALSA AudioStreamIn SET PARAMS  device %d \n\n",(1<<device));
        if(mParent->mActivatedInputDevice )//Recording stopped with Alarm ,then don't call setDevice() of record, just return
	return mParent->mInput->setDevice(mMode, 1<<device, CAPTURE);

        return NO_ERROR;
//	yman.seo        return NO_ERROR;
#endif
}

String8  AudioStreamInALSA::getParameters(const String8& keys)
{
#if defined SLSI_S5PC110
        AudioParameter param = AudioParameter(keys);
        String8 value;
        String8 key = String8(AudioParameter::keyRouting);
        
        if (param.get(key, value) == NO_ERROR) {
            param.addInt(key, (int)mDevice);
        }
        
        LOGD("AudioStreamInALSA::getParameters() %s", param.toString().string());
        return param.toString();
#else
        /* TODO: Implement as per new arch */
        return keys;
#endif
}


// ----------------------------------------------------------------------------

struct mixer_info_t
{
    mixer_info_t() :
        elem(0),
        min(SND_MIXER_VOL_RANGE_MIN),
        max(SND_MIXER_VOL_RANGE_MAX),
        mute(false)
    {
    }

    snd_mixer_elem_t *elem;
    long              min;
    long              max;
    long              volume;
    bool              mute;
    char              name[ALSA_NAME_MAX];
};

static int initMixer (snd_mixer_t **mixer, const char *name)
{
    int err;

    if ((err = snd_mixer_open(mixer, 0)) < 0) {
        LOGE("Unable to open mixer: %s", snd_strerror(err));
        return err;
    }

    if ((err = snd_mixer_attach(*mixer, name)) < 0) {
        LOGE("Unable to attach mixer to device %s: %s",
            name, snd_strerror(err));

        if ((err = snd_mixer_attach(*mixer, "hw:00")) < 0) {
            LOGE("Unable to attach mixer to device default: %s",
                snd_strerror(err));

            snd_mixer_close (*mixer);
            *mixer = NULL;
            return err;
        }
    }

    if ((err = snd_mixer_selem_register(*mixer, NULL, NULL)) < 0) {
        LOGE("Unable to register mixer elements: %s", snd_strerror(err));
        snd_mixer_close (*mixer);
        *mixer = NULL;
        return err;
    }

    // Get the mixer controls from the kernel
    if ((err = snd_mixer_load(*mixer)) < 0) {
        LOGE("Unable to load mixer elements: %s", snd_strerror(err));
        snd_mixer_close (*mixer);
        *mixer = NULL;
        return err;
    }

    return 0;
}

typedef int (*hasVolume_t)(snd_mixer_elem_t*);

static const hasVolume_t hasVolume[] = {
    snd_mixer_selem_has_playback_volume,
    snd_mixer_selem_has_capture_volume
};

typedef int (*getVolumeRange_t)(snd_mixer_elem_t*, long int*, long int*);

static const getVolumeRange_t getVolumeRange[] = {
    snd_mixer_selem_get_playback_volume_range,
    snd_mixer_selem_get_capture_volume_range
};

typedef int (*setVolume_t)(snd_mixer_elem_t*, long int);

static const setVolume_t setVol[] = {
    snd_mixer_selem_set_playback_volume_all,
    snd_mixer_selem_set_capture_volume_all
};

ALSAMixer::ALSAMixer()
{
    int err;

    initMixer (&mMixer[SND_PCM_STREAM_PLAYBACK], "AndroidPlayback");
    initMixer (&mMixer[SND_PCM_STREAM_CAPTURE], "AndroidRecord");

    snd_mixer_selem_id_t *sid;
    snd_mixer_selem_id_alloca(&sid);

    for (int i = 0; i <= SND_PCM_STREAM_LAST; i++) {

        mixer_info_t *info = mixerMasterProp[i].mInfo = new mixer_info_t;

        property_get (mixerMasterProp[i].propName,
                      info->name,
                      mixerMasterProp[i].propDefault);

        for (snd_mixer_elem_t *elem = snd_mixer_first_elem(mMixer[i]);
             elem;
             elem = snd_mixer_elem_next(elem)) {

            if (!snd_mixer_selem_is_active(elem))
                continue;

            snd_mixer_selem_get_id(elem, sid);

            // Find PCM playback volume control element.
            const char *elementName = snd_mixer_selem_id_get_name(sid);

            if (hasVolume[i] (elem))
                LOGD ("Mixer: element name: '%s'", elementName);

            if (info->elem == NULL &&
                strcmp(elementName, info->name) == 0 &&
                hasVolume[i] (elem)) {

                info->elem = elem;
                getVolumeRange[i] (elem, &info->min, &info->max);
                info->volume = info->max;
                setVol[i] (elem, info->volume);
                if (i == SND_PCM_STREAM_PLAYBACK &&
                    snd_mixer_selem_has_playback_switch (elem))
                    snd_mixer_selem_set_playback_switch_all (elem, 1);
                break;
            }
        }

        LOGD ("Mixer: master '%s' %s.", info->name, info->elem ? "found" : "not found");

        for (int j = 0; mixerProp[j][i].routes; j++) {

            mixer_info_t *info = mixerProp[j][i].mInfo = new mixer_info_t;

            property_get (mixerProp[j][i].propName,
                          info->name,
                          mixerProp[j][i].propDefault);

            for (snd_mixer_elem_t *elem = snd_mixer_first_elem(mMixer[i]);
                 elem;
                 elem = snd_mixer_elem_next(elem)) {

                if (!snd_mixer_selem_is_active(elem))
                    continue;

                snd_mixer_selem_get_id(elem, sid);

                // Find PCM playback volume control element.
                const char *elementName = snd_mixer_selem_id_get_name(sid);

               if (info->elem == NULL &&
                    strcmp(elementName, info->name) == 0 &&
                    hasVolume[i] (elem)) {

                    info->elem = elem;
                    getVolumeRange[i] (elem, &info->min, &info->max);
                    info->volume = info->max;
                    setVol[i] (elem, info->volume);
                    if (i == SND_PCM_STREAM_PLAYBACK &&
                        snd_mixer_selem_has_playback_switch (elem))
                        snd_mixer_selem_set_playback_switch_all (elem, 1);
                    break;
                }
            }
            LOGD ("Mixer: route '%s' %s.", info->name, info->elem ? "found" : "not found");
        }
    }
    LOGD("mixer initialized.");
}

ALSAMixer::~ALSAMixer()
{
    for (int i = 0; i <= SND_PCM_STREAM_LAST; i++) {
        if (mMixer[i]) snd_mixer_close (mMixer[i]);
        if (mixerMasterProp[i].mInfo) {
            delete mixerMasterProp[i].mInfo;
            mixerMasterProp[i].mInfo = NULL;
        }
        for (int j = 0; mixerProp[j][i].routes; j++) {
            if (mixerProp[j][i].mInfo) {
                delete mixerProp[j][i].mInfo;
                mixerProp[j][i].mInfo = NULL;
            }
        }
    }
    LOGD("mixer destroyed.");
}

status_t ALSAMixer::setMasterVolume(float volume)
{
    mixer_info_t *info = mixerMasterProp[SND_PCM_STREAM_PLAYBACK].mInfo;
    if (!info || !info->elem) return INVALID_OPERATION;

    long minVol = info->min;
    long maxVol = info->max;

    // Make sure volume is between bounds.
    long vol = minVol + volume * (maxVol - minVol);
    if (vol > maxVol) vol = maxVol;
    if (vol < minVol) vol = minVol;

    info->volume = vol;
    snd_mixer_selem_set_playback_volume_all (info->elem, vol);

    return NO_ERROR;
}

status_t ALSAMixer::setMasterGain(float gain)
{
    mixer_info_t *info = mixerMasterProp[SND_PCM_STREAM_CAPTURE].mInfo;
    if (!info || !info->elem) return INVALID_OPERATION;

    long minVol = info->min;
    long maxVol = info->max;

    // Make sure volume is between bounds.
    long vol = minVol + gain * (maxVol - minVol);
    if (vol > maxVol) vol = maxVol;
    if (vol < minVol) vol = minVol;

    info->volume = vol;
    snd_mixer_selem_set_capture_volume_all (info->elem, vol);

    return NO_ERROR;
}

status_t ALSAMixer::setVolume(uint32_t device, float volume)
{
    for (int j = 0; mixerProp[j][SND_PCM_STREAM_PLAYBACK].routes; j++)
        if (mixerProp[j][SND_PCM_STREAM_PLAYBACK].routes & device) {

            mixer_info_t *info = mixerProp[j][SND_PCM_STREAM_PLAYBACK].mInfo;
            if (!info || !info->elem) return INVALID_OPERATION;

            long minVol = info->min;
            long maxVol = info->max;

            // Make sure volume is between bounds.
            long vol = minVol + volume * (maxVol - minVol);
            if (vol > maxVol) vol = maxVol;
            if (vol < minVol) vol = minVol;

            info->volume = vol;
            snd_mixer_selem_set_playback_volume_all (info->elem, vol);
        }

    return NO_ERROR;
}

status_t ALSAMixer::setGain(uint32_t device, float gain)
{
    for (int j = 0; mixerProp[j][SND_PCM_STREAM_CAPTURE].routes; j++)
        if (mixerProp[j][SND_PCM_STREAM_CAPTURE].routes & device) {

            mixer_info_t *info = mixerProp[j][SND_PCM_STREAM_CAPTURE].mInfo;
            if (!info || !info->elem) return INVALID_OPERATION;

            long minVol = info->min;
            long maxVol = info->max;

            // Make sure volume is between bounds.
            long vol = minVol + gain * (maxVol - minVol);
            if (vol > maxVol) vol = maxVol;
            if (vol < minVol) vol = minVol;

            info->volume = vol;
            snd_mixer_selem_set_capture_volume_all (info->elem, vol);
        }

    return NO_ERROR;
}

status_t ALSAMixer::setCaptureMuteState(uint32_t device, bool state)
{
    for (int j = 0; mixerProp[j][SND_PCM_STREAM_CAPTURE].routes; j++)
        if (mixerProp[j][SND_PCM_STREAM_CAPTURE].routes & device) {

            mixer_info_t *info = mixerProp[j][SND_PCM_STREAM_CAPTURE].mInfo;
            if (!info || !info->elem) return INVALID_OPERATION;

            if (snd_mixer_selem_has_capture_switch (info->elem)) {

                int err = snd_mixer_selem_set_capture_switch_all (info->elem, static_cast<int>(!state));
                if (err < 0) {
                    LOGE("Unable to %s capture mixer switch %s",
                        state ? "enable" : "disable", info->name);
                    return INVALID_OPERATION;
                }
            }

            info->mute = state;
        }

    return NO_ERROR;
}

status_t ALSAMixer::getCaptureMuteState(uint32_t device, bool *state)
{
    if (! state) return BAD_VALUE;

    for (int j = 0; mixerProp[j][SND_PCM_STREAM_CAPTURE].routes; j++)
        if (mixerProp[j][SND_PCM_STREAM_CAPTURE].routes & device) {

            mixer_info_t *info = mixerProp[j][SND_PCM_STREAM_CAPTURE].mInfo;
            if (!info || !info->elem) return INVALID_OPERATION;

            *state = info->mute;
            return NO_ERROR;
        }

    return BAD_VALUE;
}

status_t ALSAMixer::setPlaybackMuteState(uint32_t device, bool state)
{

	LOGE("\n set playback mute device %d, state %d \n", device,state);

    for (int j = 0; mixerProp[j][SND_PCM_STREAM_PLAYBACK].routes; j++)
        if (mixerProp[j][SND_PCM_STREAM_PLAYBACK].routes & device) {

            mixer_info_t *info = mixerProp[j][SND_PCM_STREAM_PLAYBACK].mInfo;
            if (!info || !info->elem) return INVALID_OPERATION;

            if (snd_mixer_selem_has_playback_switch (info->elem)) {

                int err = snd_mixer_selem_set_playback_switch_all (info->elem, static_cast<int>(!state));
                if (err < 0) {
                    LOGE("Unable to %s playback mixer switch %s",
                        state ? "enable" : "disable", info->name);
                    return INVALID_OPERATION;
                }
            }

            info->mute = state;
        }

    return NO_ERROR;
}

status_t ALSAMixer::getPlaybackMuteState(uint32_t device, bool *state)
{
    if (! state) return BAD_VALUE;

    for (int j = 0; mixerProp[j][SND_PCM_STREAM_PLAYBACK].routes; j++)
        if (mixerProp[j][SND_PCM_STREAM_PLAYBACK].routes & device) {

            mixer_info_t *info = mixerProp[j][SND_PCM_STREAM_PLAYBACK].mInfo;
            if (!info || !info->elem) return INVALID_OPERATION;

            *state = info->mute;
            return NO_ERROR;
        }

    return BAD_VALUE;
}

// ----------------------------------------------------------------------------

ALSAControl::ALSAControl(const char *device)
{
    snd_ctl_open(&mHandle, device, 0);
}

ALSAControl::~ALSAControl()
{
    if (mHandle) snd_ctl_close(mHandle);
}

status_t ALSAControl::get(const char *name, unsigned int &value, int index)
{
    if (!mHandle) return NO_INIT;

    snd_ctl_elem_id_t *id;
    snd_ctl_elem_info_t *info;
    snd_ctl_elem_value_t *control;

    snd_ctl_elem_id_alloca(&id);
    snd_ctl_elem_info_alloca(&info);
    snd_ctl_elem_value_alloca(&control);
    
    snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
    snd_ctl_elem_id_set_name(id, name);
    snd_ctl_elem_info_set_id(info, id);

    int ret = snd_ctl_elem_info(mHandle, info);
    if (ret < 0) return BAD_VALUE;

    snd_ctl_elem_info_get_id(info, id);
    snd_ctl_elem_type_t type = snd_ctl_elem_info_get_type(info);
    unsigned int count = snd_ctl_elem_info_get_count(info);
    if ((unsigned int)index >= count) return BAD_VALUE;

    snd_ctl_elem_value_set_id(control, id);

    ret = snd_ctl_elem_read(mHandle, control);
    if (ret < 0) return BAD_VALUE;

    switch (type) {
        case SND_CTL_ELEM_TYPE_BOOLEAN:
            value = snd_ctl_elem_value_get_boolean(control, index);
            break;
        case SND_CTL_ELEM_TYPE_INTEGER:
            value = snd_ctl_elem_value_get_integer(control, index);
            break;
        case SND_CTL_ELEM_TYPE_INTEGER64:
            value = snd_ctl_elem_value_get_integer64(control, index);
            break;
        case SND_CTL_ELEM_TYPE_ENUMERATED:
            value = snd_ctl_elem_value_get_enumerated(control, index);
            break;
        case SND_CTL_ELEM_TYPE_BYTES:
            value = snd_ctl_elem_value_get_byte(control, index);
            break;
        default:
            return BAD_VALUE;
    }

    return NO_ERROR;
}

status_t ALSAControl::set(const char *name, unsigned int value, int index)
{
    if (!mHandle) return NO_INIT;

    snd_ctl_elem_id_t *id;
    snd_ctl_elem_info_t *info;
    snd_ctl_elem_value_t *control;

    snd_ctl_elem_id_alloca(&id);
    snd_ctl_elem_info_alloca(&info);
    snd_ctl_elem_value_alloca(&control);
	
    snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
    snd_ctl_elem_id_set_name(id, name);
    snd_ctl_elem_info_set_id(info, id);

    int ret = snd_ctl_elem_info(mHandle, info);
    if (ret < 0) return BAD_VALUE;

    snd_ctl_elem_info_get_id(info, id);
    snd_ctl_elem_type_t type = snd_ctl_elem_info_get_type(info);
    unsigned int count = snd_ctl_elem_info_get_count(info);
    if ((unsigned int)index >= count) return BAD_VALUE;

    if (index == -1)
        index = 0; // Range over all of them
    else
        count = index + 1; // Just do the one specified

    snd_ctl_elem_value_set_id(control, id);

    for (unsigned int i = index; i < count; i++)
        switch (type) {
            case SND_CTL_ELEM_TYPE_BOOLEAN:
                snd_ctl_elem_value_set_boolean(control, i, value);
                break;
            case SND_CTL_ELEM_TYPE_INTEGER:
                snd_ctl_elem_value_set_integer(control, i, value);
                break;
            case SND_CTL_ELEM_TYPE_INTEGER64:
                snd_ctl_elem_value_set_integer64(control, i, value);
                break;
            case SND_CTL_ELEM_TYPE_ENUMERATED:
                snd_ctl_elem_value_set_enumerated(control, i, value);
                break;
            case SND_CTL_ELEM_TYPE_BYTES:
                snd_ctl_elem_value_set_byte(control, i, value);
                break;
            default:
                break;
        }

    ret = snd_ctl_elem_write(mHandle, control);
    return (ret < 0) ? BAD_VALUE : NO_ERROR;
}

// ----------------------------------------------------------------------------

#if defined SEC_IPC
AudioHardwareIPC::AudioHardwareIPC() :
    mClient(NULL)
{
LOGD("### %s", __func__);
    int err = 0;

    mClient = OpenClient_RILD();
    if (mClient == NULL){
        LOGE("[*] OpenClient_RILD() error\n");
        err = 1;
    }

    if (RegisterRequestCompleteHandler(mClient, RIL_REQUEST_OEM_HOOK_RAW,
                onRawReqComplete) != RIL_CLIENT_ERR_SUCCESS){
        LOGE("[*] RegisterRequestCompleteHandler() error\n");
        err = 1;
    }

    if (RegisterUnsolicitedHandler(mClient, 11004, onUnsol) !=
                RIL_CLIENT_ERR_SUCCESS){
        LOGE("[*] RegisterUnsolicitedHandler() error\n");
        err = 1;
    }

    if (!err) LOGD("Success Initializing IPC");
    else LOGE("Failed Initializing IPC");
}

AudioHardwareIPC::~AudioHardwareIPC()
{
    LOGD("### %s", __func__);
    if (RegisterRequestCompleteHandler(mClient, RIL_REQUEST_OEM_HOOK_RAW, NULL) != RIL_CLIENT_ERR_SUCCESS)
        LOGE("RegisterRequestCompleteHandler(NULL) error\n");

    if (RegisterUnsolicitedHandler(mClient, 11004, NULL) != RIL_CLIENT_ERR_SUCCESS)
        LOGE("RegisterUnsolicitedHandler(NULL) error\n");

    if (Disconnect_RILD(mClient) != RIL_CLIENT_ERR_SUCCESS)
        LOGE("[*] Disconnect_RILD() error\n");

    if (CloseClient_RILD(mClient) != RIL_CLIENT_ERR_SUCCESS)
        LOGE("[*] CloseClient_RILD() error\n");
    mClient = NULL;
}

status_t AudioHardwareIPC::transmitVolumeIPC(uint32_t type, float volume)
{
    int ret = 0;
    uint32_t level = (uint32_t)(volume * 5);

     if (isConnected_RILD(mClient) == 0) {
         if (Connect_RILD(mClient) != RIL_CLIENT_ERR_SUCCESS){
             LOGE("[*] Connect_RILD() error\n");
             return INVALID_OPERATION;
         }
     }

    memset(data, 0, 100);
    data[0] = OEM_FUNCTION_ID_SOUND;
    data[1] = OEM_SOUND_SET_VOLUME_CTRL;
    data[2] = 0x00; // data length 
    data[3] = 0x06; // data length
    data[4] = type; // volume type
    data[5] = level; // volume level

    ret = InvokeOemRequestHookRaw(mClient, data, 6); //sizeof(data));
    if (ret != RIL_CLIENT_ERR_AGAIN && ret != RIL_CLIENT_ERR_SUCCESS){
        LOGE("[*] InvokeOemRequestHookRaw() error ret = %d\n", ret);
        return INVALID_OPERATION;
    }
    return NO_ERROR;
}

status_t AudioHardwareIPC::transmitAudioPathIPC(uint32_t path)
{
    int ret = 0;

	LOGI("### %s %d ", __func__, path);
     if (isConnected_RILD(mClient) == 0) {
         if (Connect_RILD(mClient) != RIL_CLIENT_ERR_SUCCESS){
             LOGE("[*] Connect_RILD() error\n");
             return INVALID_OPERATION;
         }
     }

    memset(data, 0, 100);
    data[0] = OEM_FUNCTION_ID_SOUND;
    data[1] = OEM_SOUND_SET_AUDIO_PATH_CTRL;
    data[2] = 0x00; // data length 
    data[3] = 0x05; // data length
    data[4] = path; // audio path

    ret = InvokeOemRequestHookRaw(mClient, data, 5); //sizeof(data));
    if (ret != RIL_CLIENT_ERR_AGAIN && ret != RIL_CLIENT_ERR_SUCCESS){
        LOGE("[*] InvokeOemRequestHookRaw() error ret = %d\n", ret);
        return INVALID_OPERATION;
    }
    return NO_ERROR;
}


#if defined SYNCHRONIZE_CP
status_t AudioHardwareIPC::transmitClock_IPC(uint32_t condition)
{
	int ret = 0;

	LOGV("### %s %d ", __func__, condition);

	if (isConnected_RILD(mClient) == 0) {
		if (Connect_RILD(mClient) != RIL_CLIENT_ERR_SUCCESS){
			LOGE("[*] Connect_RILD() error\n");
			return INVALID_OPERATION;
		}
	}

	memset(data, 0, 100);
	data[0] = OEM_FUNCTION_ID_SOUND;
	data[1] = OEM_SOUND_SET_CLOCK_CTRL;
	data[2] = 0x00; // data length 
	data[3] = 0x05; // data length
	data[4] = condition; 

	ret = InvokeOemRequestHookRaw(mClient, data, 5); //sizeof(data));
 
	if (ret != RIL_CLIENT_ERR_AGAIN && ret != RIL_CLIENT_ERR_SUCCESS){
		LOGE("[*] InvokeOemRequestHookRaw() error ret = %d\n", ret);
		return INVALID_OPERATION;
	}

	return NO_ERROR;
}
#endif

static int onRawReqComplete(HRilClient client, const void *data, size_t datalen)
{
        LOGV("[*] %s(): datalen(%d)\n", __FUNCTION__, datalen);
        return 0;
}
static int onUnsol(HRilClient client, const void *data, size_t datalen)
{
        int a;
        a = ((int *)data)[0];
		LOGV("%s(): a(%d)\n", __FUNCTION__, a);

        return 0;
}
#endif //SEC_IPC
};        // namespace android

