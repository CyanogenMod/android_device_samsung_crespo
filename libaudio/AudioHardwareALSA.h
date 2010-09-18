/* AudioHardwareALSA.h
 **
 ** Copyright 2008, Wind River Systems
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

#ifndef ANDROID_AUDIO_HARDWARE_ALSA_H
#define ANDROID_AUDIO_HARDWARE_ALSA_H

#include <stdint.h>
#include <sys/types.h>
#include <alsa/asoundlib.h>

#include <hardware_legacy/AudioHardwareBase.h>

#if defined SEC_IPC
#include <hardware/hardware.h>

// sangsu fix : headers for IPC
#include "secril-client.h"

// sangsu fix : defines for IPC
#define OEM_FUNCTION_ID_SOUND 0x08 // sound Main Cmd

//sangsu fix : sound sub command for IPC
#define OEM_SOUND_SET_VOLUME_CTRL 0x03
#define OEM_SOUND_GET_VOLUME_CTRL 0x04
#define OEM_SOUND_SET_AUDIO_PATH_CTRL 0x05
#define OEM_SOUND_GET_AUDIO_PATH_CTRL 0x06 

//sangsu fix : audio path for IPC
#define OEM_SOUND_AUDIO_PATH_HANDSET     0x01 
#define OEM_SOUND_AUDIO_PATH_HEADSET     0x02 
#define OEM_SOUND_AUDIO_PATH_HANDFREE   0x03 
#define OEM_SOUND_AUDIO_PATH_BLUETOOTH 0x04
#define OEM_SOUND_AUDIO_PATH_STREOBT      0x05
#define OEM_SOUND_AUDIO_PATH_SPEAKER      0x06 
#define OEM_SOUND_AUDIO_PATH_HEADSET35 0x07 
#define OEM_SOUND_AUDIO_PATH_BT_NSEC_OFF 0x08

// sangsu fix : volume level for IPC
#define OEM_SOUND_VOLUME_LEVEL_MUTE  0x00 
#define OEM_SOUND_VOLUME_LEVEL1           0x01
#define OEM_SOUND_VOLUME_LEVEL2           0x02
#define OEM_SOUND_VOLUME_LEVEL3           0x03
#define OEM_SOUND_VOLUME_LEVEL4           0x04
#define OEM_SOUND_VOLUME_LEVEL5           0x05
#define OEM_SOUND_VOLUME_LEVEL6           0x06
#define OEM_SOUND_VOLUME_LEVEL7           0x07
#define OEM_SOUND_VOLUME_LEVEL8           0x08

// For synchronizing I2S clocking
#if defined SYNCHRONIZE_CP
#define OEM_SOUND_SET_CLOCK_CTRL	0x0A
#define OEM_SOUND_CLOCK_START		0x01
#define OEM_SOUND_CLOCK_STOP		0x00
#endif

// For VT
#if defined VIDEO_TELEPHONY
#define OEM_SOUND_VIDEO_CALL_STOP	0x00
#define OEM_SOUND_VIDEO_CALL_START	0x01
#define OEM_SOUND_SET_VIDEO_CALL_CTRL	0x07
#endif

// sangsu fix : volume type for IPC
#define OEM_SOUND_TYPE_VOICE           0x01 // Receiver(0x00) + Voice(0x01)
#define OEM_SOUND_TYPE_KEYTONE      0x02 // Receiver(0x00) + Key tone (0x02)
#define OEM_SOUND_TYPE_BELL             0x03 // Receiver(0x00) + Bell (0x03)
#define OEM_SOUND_TYPE_MESSAGE      0x04 // Receiver(0x00) + Message(0x04)
#define OEM_SOUND_TYPE_ALARM          0x05 // Receiver(0x00) + Alarm (0x05)
#define OEM_SOUND_TYPE_SPEAKER       0x11 // SpeakerPhone (0x10) + Voice(0x01)
#define OEM_SOUND_TYPE_HFKVOICE     0x21 // HFK   (0x20) + Voice(0x01)
#define OEM_SOUND_TYPE_HFKKEY         0x22 // HFK   (0x20) + Key tone (0x02)
#define OEM_SOUND_TYPE_HFKBELL        0x23 // HFK   (0x20) + Bell (0x03)
#define OEM_SOUND_TYPE_HFKMSG         0x24 // HFK   (0x20) + Message(0x04)
#define OEM_SOUND_TYPE_HFKALARM    0x25 // HFK   (0x20) + Alarm (0x05)
#define OEM_SOUND_TYPE_HFKPDA         0x26 // HFK   (0x20) + PDA miscellaneous sound (0x06)
#define OEM_SOUND_TYPE_HEADSET       0x31 // Headset (0x30) + Voice(0x01)
#define OEM_SOUND_TYPE_BTVOICE        0x41 // BT(0x40) + Voice(0x01)
#endif
namespace android
{

    class AudioHardwareALSA;

    // ----------------------------------------------------------------------------

    class ALSAMixer
    {
        public:
                                    ALSAMixer();
            virtual                ~ALSAMixer();

            bool                    isValid() { return !!mMixer[SND_PCM_STREAM_PLAYBACK]; }
            status_t                setMasterVolume(float volume);
            status_t                setMasterGain(float gain);

            status_t                setVolume(uint32_t device, float volume);
            status_t                setGain(uint32_t device, float gain);

            status_t                setCaptureMuteState(uint32_t device, bool state);
            status_t                getCaptureMuteState(uint32_t device, bool *state);
            status_t                setPlaybackMuteState(uint32_t device, bool state);
            status_t                getPlaybackMuteState(uint32_t device, bool *state);

        private:
            snd_mixer_t            *mMixer[SND_PCM_STREAM_LAST+1];
    };

    class ALSAControl
    {
        public:
                                    ALSAControl(const char *device = "default");
            virtual                ~ALSAControl();

            status_t                get(const char *name, unsigned int &value, int index = 0);
            status_t                set(const char *name, unsigned int value, int index = -1);

        private:
            snd_ctl_t              *mHandle;
    };

    class ALSAStreamOps
    {
        protected:
            friend class AudioStreamOutALSA;
            friend class AudioStreamInALSA;

            struct StreamDefaults
            {
                const char *        devicePrefix;
                snd_pcm_stream_t    direction;       // playback or capture
                snd_pcm_format_t    format;
                int                 channels;
                uint32_t            sampleRate;
                unsigned int        latency;         // Delay in usec
                unsigned int        bufferSize;      // Size of sample buffer
            };

                                    ALSAStreamOps();
            virtual                ~ALSAStreamOps();

            status_t                set(int *format,
                                        uint32_t *channels,
                                        uint32_t *rate);
            virtual uint32_t        sampleRate() const;
            status_t                sampleRate(uint32_t rate);
            virtual size_t          bufferSize() const;
            virtual int             format() const;
	    int 		    getAndroidFormat(snd_pcm_format_t format);

            virtual int             channelCount() const;
            status_t                channelCount(int channels);
	    uint32_t		    getAndroidChannels(int channels);
                  
            status_t                open(int mode, uint32_t device);
            void                    close();
            status_t                setSoftwareParams();
            status_t                setPCMFormat(snd_pcm_format_t format);
            status_t                setHardwareResample(bool resample);

            const char             *streamName();
            virtual status_t        setDevice(int mode, uint32_t device, uint32_t audio_mode);

            const char             *deviceName(int mode, uint32_t device);

            void                    setStreamDefaults(StreamDefaults *dev) {
                mDefaults = dev;
            }

            Mutex                   mLock;

        private:
            snd_pcm_t              *mHandle;
            snd_pcm_hw_params_t    *mHardwareParams;
            snd_pcm_sw_params_t    *mSoftwareParams;
            int                     mMode;
            uint32_t                mDevice;

            StreamDefaults         *mDefaults;
    };

    // ----------------------------------------------------------------------------

    class AudioStreamOutALSA : public AudioStreamOut, public ALSAStreamOps
    {
        public:
                                    AudioStreamOutALSA(AudioHardwareALSA *parent);
            virtual                ~AudioStreamOutALSA();


	    status_t		     set(int *format,
					 uint32_t *channelCount,
					 uint32_t *sampleRate){
		return ALSAStreamOps::set(format, channelCount, sampleRate);
	    }

            virtual uint32_t        sampleRate() const
            {
                return ALSAStreamOps::sampleRate();
            }

            virtual size_t          bufferSize() const
            {
                return ALSAStreamOps::bufferSize();
            }

            //virtual int             channelCount() const;
            virtual uint32_t             channels() const;

            virtual int             format() const
            {
                return ALSAStreamOps::format();
            }

            virtual uint32_t        latency() const;

            virtual ssize_t         write(const void *buffer, size_t bytes);
            virtual status_t        dump(int fd, const Vector<String16>& args);
            virtual status_t        setDevice(int mode, uint32_t newDevice, uint32_t audio_mode);
	    virtual status_t    setVolume(float left, float right); //Tushar: New arch

            status_t                setVolume(float volume);

            status_t                standby();
            bool                    isStandby();

			virtual status_t    setParameters(const String8& keyValuePairs);
			virtual String8     getParameters(const String8& keys);
	
			virtual status_t    getRenderPosition(uint32_t *dspFrames);


        private:
            AudioHardwareALSA      *mParent;
            bool                    mPowerLock;
    };

    class AudioStreamInALSA : public AudioStreamIn, public ALSAStreamOps
    {
        public:
                                    AudioStreamInALSA(AudioHardwareALSA *parent);
            virtual                ~AudioStreamInALSA();

	    status_t		     set(int *format,
					 uint32_t *channelCount,
					 uint32_t *sampleRate){
		return ALSAStreamOps::set(format, channelCount, sampleRate);
	    }

            //virtual uint32_t        sampleRate() {
            virtual uint32_t        sampleRate() const {
                return ALSAStreamOps::sampleRate();
            }

            virtual size_t          bufferSize() const
            {
                return ALSAStreamOps::bufferSize();
            }

            //virtual int             channelCount() const
            virtual uint32_t             channels() const
            {
                return ALSAStreamOps::channelCount();
            }

            virtual int             format() const
            {
                return ALSAStreamOps::format();
            }

            virtual ssize_t         read(void* buffer, ssize_t bytes);
            virtual status_t        dump(int fd, const Vector<String16>& args);
            virtual status_t        setDevice(int mode, uint32_t newDevice, uint32_t audio_mode);

            virtual status_t        setGain(float gain);

            virtual status_t        standby();

	    virtual status_t    setParameters(const String8& keyValuePairs);
	    virtual String8     getParameters(const String8& keys);

		virtual unsigned int  getInputFramesLost() const { return 0; }

        private:
            AudioHardwareALSA *mParent;
            bool               mPowerLock;
    };

#if defined SEC_IPC
    //TODO..implementation has to be done
    class AudioHardwareIPC
    {
	public:
					AudioHardwareIPC();
		virtual		~AudioHardwareIPC(); 	
		status_t		transmitVolumeIPC(uint32_t type, float volume);
		status_t		transmitAudioPathIPC(uint32_t path);
#if defined SYNCHRONIZE_CP
		status_t		transmitClock_IPC(uint32_t condition);
#endif
	private:
		HRilClient		mClient;
		char			data[100];	
    };
#endif
    class AudioHardwareALSA : public AudioHardwareBase
    {
        public:
                                    AudioHardwareALSA();
            virtual                ~AudioHardwareALSA();

            /**
             * check to see if the audio hardware interface has been initialized.
             * return status based on values defined in include/utils/Errors.h
             */
            virtual status_t        initCheck();

            /**
             * put the audio hardware into standby mode to conserve power. Returns
             * status based on include/utils/Errors.h
             */
            virtual status_t        standby();

            /** set the audio volume of a voice call. Range is between 0.0 and 1.0 */
            virtual status_t        setVoiceVolume(float volume);

            /**
             * set the audio volume for all audio activities other than voice call.
             * Range between 0.0 and 1.0. If any value other than NO_ERROR is returned,
             * the software mixer will emulate this capability.
             */
            virtual status_t        setMasterVolume(float volume);

            // mic mute
            virtual status_t        setMicMute(bool state);
            virtual status_t        getMicMute(bool* state);
		virtual size_t getInputBufferSize(
			uint32_t sampleRate,
			int format,
			int channelCount);
#if defined TURN_ON_DEVICE_ONLY_USE
                virtual int             setMicStatus(int on);   // To deliver status of input stream(activated or not). If it's activated, doesn't turn off codec.
#endif
 	   /** This method creates and opens the audio hardware output stream */
	    virtual AudioStreamOut* openOutputStream(
                                uint32_t devices,
                                int *format=0,
                                uint32_t *channels=0,
                                uint32_t *sampleRate=0,
                                status_t *status=0);
	    virtual    void        closeOutputStream(AudioStreamOut* out);

	    /** This method creates and opens the audio hardware input stream */
	    virtual AudioStreamIn* openInputStream(
                                uint32_t devices,
                                int *format,
                                uint32_t *channels,
                                uint32_t *sampleRate,
                                status_t *status,
                                AudioSystem::audio_in_acoustics acoustics);
	    virtual    void        closeInputStream(AudioStreamIn* in);



        protected:
            /**
             * doRouting actually initiates the routing. A call to setRouting
             * or setMode may result in a routing change. The generic logic calls
             * doRouting when required. If the device has any special requirements these
             * methods can be overriden.
             */
            virtual status_t    doRouting(uint32_t device);

            virtual status_t    dump(int fd, const Vector<String16>& args);

            friend class AudioStreamOutALSA;
            friend class AudioStreamInALSA;

            ALSAMixer          *mMixer;
            AudioStreamOutALSA *mOutput;
            AudioStreamInALSA  *mInput;
#if defined SEC_IPC
	    AudioHardwareIPC   *mIPC; //for IPC	    
	    uint32_t        mRoutes[AudioSystem::NUM_MODES];
#endif

        private:
            Mutex               mLock;
#if defined TURN_ON_DEVICE_ONLY_USE
                bool            mActivatedInputDevice;
#endif
    };

    // ----------------------------------------------------------------------------

#if defined SEC_IPC
// sangsu fix : global functions for IPC
static int onRawReqComplete(HRilClient client, const void *data, size_t datalen);
static int onUnsol(HRilClient client, const void *data, size_t datalen);
#endif
};        // namespace android
#endif    // ANDROID_AUDIO_HARDWARE_ALSA_H
