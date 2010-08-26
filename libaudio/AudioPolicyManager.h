/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include <stdint.h>
#include <sys/types.h>
#include <utils/Timers.h>
#include <utils/Errors.h>
#include <utils/KeyedVector.h>
#include <hardware_legacy/AudioPolicyInterface.h>


namespace android {

// ----------------------------------------------------------------------------

#define MAX_DEVICE_ADDRESS_LEN 20
// Attenuation applied to STRATEGY_SONIFICATION streams when a headset is connected: 6dB
#define SONIFICATION_HEADSET_VOLUME_FACTOR 0.5
// Min volume for STRATEGY_SONIFICATION streams when limited by music volume: -36dB
#define SONIFICATION_HEADSET_VOLUME_MIN  0.016
// Time in seconds during which we consider that music is still active after a music
// track was stopped - see computeVolume()
#define SONIFICATION_HEADSET_MUSIC_DELAY  5
class AudioPolicyManager: public AudioPolicyInterface
{

public:
                AudioPolicyManager(AudioPolicyClientInterface *clientInterface);
        virtual ~AudioPolicyManager();

        // AudioPolicyInterface
        virtual status_t setDeviceConnectionState(AudioSystem::audio_devices device,
                                                          AudioSystem::device_connection_state state,
                                                          const char *device_address);
        virtual AudioSystem::device_connection_state getDeviceConnectionState(AudioSystem::audio_devices device,
                                                                              const char *device_address);
        virtual void setPhoneState(int state);
        virtual void setRingerMode(uint32_t mode, uint32_t mask);
        virtual void setForceUse(AudioSystem::force_use usage, AudioSystem::forced_config config);
        virtual AudioSystem::forced_config getForceUse(AudioSystem::force_use usage);
        virtual void setSystemProperty(const char* property, const char* value);
        virtual audio_io_handle_t getOutput(AudioSystem::stream_type stream,
                                            uint32_t samplingRate,
                                            uint32_t format,
                                            uint32_t channels,
                                            AudioSystem::output_flags flags);
        virtual status_t startOutput(audio_io_handle_t output, AudioSystem::stream_type stream,int session);
        virtual status_t stopOutput(audio_io_handle_t output, AudioSystem::stream_type stream,int session);
        virtual void releaseOutput(audio_io_handle_t output);
        virtual audio_io_handle_t getInput(int inputSource,
                                            uint32_t samplingRate,
                                            uint32_t format,
                                            uint32_t channels,
                                            AudioSystem::audio_in_acoustics acoustics);
        // indicates to the audio policy manager that the input starts being used.
        virtual status_t startInput(audio_io_handle_t input);
        // indicates to the audio policy manager that the input stops being used.
        virtual status_t stopInput(audio_io_handle_t input);
        virtual void releaseInput(audio_io_handle_t input);
        virtual void initStreamVolume(AudioSystem::stream_type stream,
                                                    int indexMin,
                                                    int indexMax);
        virtual status_t setStreamVolumeIndex(AudioSystem::stream_type stream, int index);
        virtual status_t getStreamVolumeIndex(AudioSystem::stream_type stream, int *index);

    	// return the strategy corresponding to a given stream type
    	virtual uint32_t getStrategyForStream(AudioSystem::stream_type stream) ;

    	// Audio effect management
    	virtual audio_io_handle_t getOutputForEffect(effect_descriptor_t *desc) ;
    	virtual status_t registerEffect(effect_descriptor_t *desc,
                                    audio_io_handle_t output,
                                    uint32_t strategy,
                                    int session,
                                    int id) ;
    	virtual status_t unregisterEffect(int id) ;

        virtual status_t dump(int fd);

private:

        enum routing_strategy {
            STRATEGY_MEDIA,
            STRATEGY_PHONE,
            STRATEGY_SONIFICATION,
            STRATEGY_DTMF,
            NUM_STRATEGIES
        };

        // descriptor for audio outputs. Used to maintain current configuration of each opened audio output
        // and keep track of the usage of this output by each audio stream type.
        class AudioOutputDescriptor
        {
        public:
            AudioOutputDescriptor();

            status_t    dump(int fd);

            uint32_t device();
            void changeRefCount(AudioSystem::stream_type, int delta);
            bool isUsedByStrategy(routing_strategy strategy);
            bool isUsedByStream(AudioSystem::stream_type stream) { return mRefCount[stream] > 0 ? true : false; }
            bool isDuplicated() { return (mDevice == 0); } // by convention mDevice is 0 for duplicated outputs

            uint32_t mSamplingRate;             //
            uint32_t mFormat;                   //
            uint32_t mChannels;                 // output configuration
            uint32_t mLatency;                  //
            AudioSystem::output_flags mFlags;   //
            uint32_t mDevice;                   // current device this output is routed to
            uint32_t mRefCount[AudioSystem::NUM_STREAM_TYPES]; // number of streams of each type using this output
            AudioOutputDescriptor *mOutput1;    // used by duplicated outputs: first output
            AudioOutputDescriptor *mOutput2;    // used by duplicated outputs: second output
            float mCurVolume[AudioSystem::NUM_STREAM_TYPES];   // current stream volume
        };

        // descriptor for audio inputs. Used to maintain current configuration of each opened audio input
        // and keep track of the usage of this input.
        class AudioInputDescriptor
        {
        public:
            AudioInputDescriptor();

            status_t    dump(int fd);

            uint32_t mSamplingRate;                     //
            uint32_t mFormat;                           // input configuration
            uint32_t mChannels;                         //
            AudioSystem::audio_in_acoustics mAcoustics; //
            uint32_t mDevice;                           // current device this input is routed to
            uint32_t mRefCount;                         // number of AudioRecord clients using this output
            int      mInputSource;                     // input source selected by application (mediarecorder.h)
        };

        // stream descriptor used for volume control
        class StreamDescriptor
        {
        public:
            StreamDescriptor()
            :   mIndexMin(0), mIndexMax(1), mIndexCur(1), mMuteCount(0), mCanBeMuted(true) {}

            void dump(char* buffer, size_t size);

            int mIndexMin;      // min volume index
            int mIndexMax;      // max volume index
            int mIndexCur;      // current volume index
            int mMuteCount;     // mute request counter
            bool mCanBeMuted;   // true is the stream can be muted
        };

        // return the strategy corresponding to a given stream type
        static routing_strategy getStrategy(AudioSystem::stream_type stream);
        // return the output handle of an output routed to the specified device, 0 if no output
        // is routed to the device
        audio_io_handle_t getOutputForDevice(uint32_t device);
        // return appropriate device for streams handled by the specified strategy according to current
        // phone state, connected devices...
        uint32_t getDeviceForStrategy(routing_strategy strategy);
        // change the route of the specified output
        void setOutputDevice(audio_io_handle_t output, uint32_t device, bool force = false, int delayMs = 0);
        // select input device corresponding to requested audio source
        uint32_t getDeviceForInputSource(int inputSource);
        // return io handle of active input or 0 if no input is active
        audio_io_handle_t getActiveInput();
        // compute the actual volume for a given stream according to the requested index and a particular
        // device
        float computeVolume(int stream, int index, audio_io_handle_t output, uint32_t device);
        // check that volume change is permitted, compute and send new volume to audio hardware
        status_t checkAndSetVolume(int stream, int index, audio_io_handle_t output, uint32_t device, int delayMs = 0, bool force = false);
        // apply all stream volumes to the specified output and device
        void applyStreamVolumes(audio_io_handle_t output, uint32_t device, int delayMs = 0);
        // Mute or unmute all streams handled by the specified strategy on the specified output
        void setStrategyMute(routing_strategy strategy, bool on, audio_io_handle_t output, int delayMs = 0);
        // Mute or unmute the stream on the specified output
        void setStreamMute(int stream, bool on, audio_io_handle_t output, int delayMs = 0);
        // handle special cases for sonification strategy while in call: mute streams or replace by
        // a special tone in the device used for communication
        void handleIncallSonification(int stream, bool starting, bool stateChange);

        uint32_t getMaxEffectsCpuLoad();
        uint32_t getMaxEffectsMemory();
        AudioPolicyClientInterface *mpClientInterface;  // audio policy client interface
        audio_io_handle_t mHardwareOutput;              // hardware output handler
        audio_io_handle_t mA2dpOutput;                  // A2DP output handler
        audio_io_handle_t mDuplicatedOutput;            // duplicated output handler: outputs to hardware and A2DP.

        KeyedVector<audio_io_handle_t, AudioOutputDescriptor *> mOutputs;   // list of output descriptors
        KeyedVector<audio_io_handle_t, AudioInputDescriptor *> mInputs;     // list of input descriptors
        uint32_t mAvailableOutputDevices;                                   // bit field of all available output devices
        uint32_t mAvailableInputDevices;                                    // bit field of all available input devices
        int mPhoneState;                                                    // current phone state
        uint32_t                 mRingerMode;                               // current ringer mode
        AudioSystem::forced_config mForceUse[AudioSystem::NUM_FORCE_USE];   // current forced use configuration

        StreamDescriptor mStreams[AudioSystem::NUM_STREAM_TYPES];           // stream descriptors for volume control
        String8 mA2dpDeviceAddress;                                         // A2DP device MAC address
        String8 mScoDeviceAddress;                                          // SCO device MAC address
        nsecs_t mMusicStopTime;                                             // time when last music stream was stopped
        bool    mLimitRingtoneVolume;                                       // limit ringtone volume to music volume if headset connected
	uint32_t mTotalEffectsCpuLoad;	
	uint32_t mTotalEffectsMemory;	
};

};
