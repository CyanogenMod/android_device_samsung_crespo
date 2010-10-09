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

// sangsu fix : headers for IPC
#include "secril-client.h"

#ifndef ALSA_DEFAULT_SAMPLE_RATE
#define ALSA_DEFAULT_SAMPLE_RATE 44100 // in Hz
#endif

#define DEFAULT_SAMPLE_RATE ALSA_DEFAULT_SAMPLE_RATE

#define PLAYBACK    0
#define PERIOD_SZ_PLAYBACK   1024
#define PERIODS_PLAYBACK     4
#define BUFFER_SZ_PLAYBACK   (PERIODS_PLAYBACK * PERIOD_SZ_PLAYBACK)
#define LATENCY_PLAYBACK_MS  ((BUFFER_SZ_PLAYBACK * 1000 / DEFAULT_SAMPLE_RATE) * 1000)

#define CAPTURE     1
#define PERIOD_SZ_CAPTURE   2048
#define PERIODS_CAPTURE     2
#define BUFFER_SZ_CAPTURE   (PERIODS_CAPTURE * PERIOD_SZ_CAPTURE)
#define LATENCY_CAPTURE_MS  ((BUFFER_SZ_CAPTURE * 1000 / DEFAULT_SAMPLE_RATE) * 1000)

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
     public:
                uint32_t            device() { return mDevice; }
                void                close();

        protected:
            friend class AudioStreamOutALSA;
            friend class AudioStreamInALSA;

            struct StreamDefaults
            {
                const char *        devicePrefix;
                snd_pcm_stream_t    direction;       // playback or capture
                snd_pcm_format_t    format;
                int                 channelCount;
                uint32_t            sampleRate;
                uint32_t            smpRateShift;
                unsigned int        latency;         // Delay in usec
                unsigned int        bufferSize;      // Size of sample buffer
                unsigned int        periodSize;      // Size of sample buffer
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
                    int             getAndroidFormat(snd_pcm_format_t format);

            virtual uint32_t        channels() const;
                    int             channelCount() const;
                    status_t        channelCount(int channelCount);
                    uint32_t        getAndroidChannels(int channelCount) const;

            status_t                open(int mode, uint32_t device);
            status_t                setSoftwareParams();
            status_t                setPCMFormat(snd_pcm_format_t format);
            status_t                setHardwareResample(bool resample);

            const char             *streamName();
            status_t                setDevice(int mode, uint32_t device, uint32_t audio_mode);

            const char             *deviceName(int mode, uint32_t device);

            void                    setStreamDefaults(StreamDefaults *dev) {
                mDefaults = dev;
            }

            Mutex                   mLock;

        private:
            snd_pcm_t              *mHandle;
            snd_pcm_hw_params_t    *mHardwareParams;
            snd_pcm_sw_params_t    *mSoftwareParams;
            uint32_t                mDevice;

            StreamDefaults         *mDefaults;
    };

    // ----------------------------------------------------------------------------

    class AudioStreamOutALSA : public AudioStreamOut, public ALSAStreamOps
    {
        public:
                                    AudioStreamOutALSA(AudioHardwareALSA *parent);
            virtual                ~AudioStreamOutALSA();


            status_t                set(int *format,
                                        uint32_t *channelCount,
                                        uint32_t *sampleRate){
                return ALSAStreamOps::set(format, channelCount, sampleRate);
            }

            virtual uint32_t        sampleRate() const {
                return ALSAStreamOps::sampleRate();
            }

            virtual size_t          bufferSize() const
            {
                return ALSAStreamOps::bufferSize();
            }

            virtual uint32_t        channels() const
            {
                return ALSAStreamOps::channels();
            }

            virtual int             format() const
            {
                return ALSAStreamOps::format();
            }

            virtual uint32_t        latency() const;

            virtual ssize_t         write(const void *buffer, size_t bytes);
            virtual status_t        dump(int fd, const Vector<String16>& args);
                    status_t        setDevice(int mode, uint32_t newDevice, uint32_t audio_mode,
                                              bool force = false);
            virtual status_t        setVolume(float left, float right); //Tushar: New arch

            status_t                setVolume(float volume);

            status_t                standby();

            virtual status_t        setParameters(const String8& keyValuePairs);
            virtual String8         getParameters(const String8& keys);

            virtual status_t        getRenderPosition(uint32_t *dspFrames);
                    bool            isActive() { return mPowerLock; }

        private:
            AudioHardwareALSA      *mParent;
            bool                    mPowerLock;
    };

    class AudioStreamInALSA : public AudioStreamIn, public ALSAStreamOps
    {
        public:
                                    AudioStreamInALSA(AudioHardwareALSA *parent);
            virtual                ~AudioStreamInALSA();

            status_t                set(int *format,
                                        uint32_t *channelCount,
                                        uint32_t *sampleRate) {
                return ALSAStreamOps::set(format, channelCount, sampleRate);
            }

            virtual uint32_t        sampleRate() const {
                return ALSAStreamOps::sampleRate();
            }

            virtual size_t          bufferSize() const
            {
                return ALSAStreamOps::bufferSize();
            }

            virtual uint32_t        channels() const
            {
                return ALSAStreamOps::channels();
            }

            virtual int             format() const
            {
                return ALSAStreamOps::format();
            }

            virtual ssize_t         read(void* buffer, ssize_t bytes);
            virtual status_t        dump(int fd, const Vector<String16>& args);
                    status_t        setDevice(int mode, uint32_t newDevice, uint32_t audio_mode,
                                              bool force = false);

            virtual status_t        setGain(float gain);

            virtual status_t        standby();

            virtual status_t        setParameters(const String8& keyValuePairs);
            virtual String8         getParameters(const String8& keys);

            virtual unsigned int    getInputFramesLost() const { return 0; }

                    bool            isActive() { return mPowerLock; }

        private:
            AudioHardwareALSA      *mParent;
            bool                    mPowerLock;
            int16_t                 mBuffer[2 * PERIOD_SZ_CAPTURE];
    };

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

            /** set the audio volume of a voice call. Range is between 0.0 and 1.0 */
            virtual status_t        setVoiceVolume(float volume);

            virtual status_t        setMode(int mode);

            /**
             * set the audio volume for all audio activities other than voice call.
             * Range between 0.0 and 1.0. If any value other than NO_ERROR is returned,
             * the software mixer will emulate this capability.
             */
            virtual status_t        setMasterVolume(float volume);

            // mic mute
            virtual status_t        setMicMute(bool state);
            virtual status_t        getMicMute(bool* state);
            virtual size_t          getInputBufferSize(uint32_t sampleRate,
                                                       int format,
                                                       int channelCount);
            virtual int             setMicStatus(int on);   // To deliver status of input stream(activated or not). If it's activated, doesn't turn off codec.

            /** This method creates and opens the audio hardware output stream */
            virtual AudioStreamOut* openOutputStream(uint32_t devices,
                                                     int *format = 0,
                                                     uint32_t *channels = 0,
                                                     uint32_t *sampleRate = 0,
                                                     status_t *status = 0);
            virtual void            closeOutputStream(AudioStreamOut* out);

            /** This method creates and opens the audio hardware input stream */
            virtual AudioStreamIn*  openInputStream(uint32_t devices,
                                                   int *format,
                                                   uint32_t *channels,
                                                   uint32_t *sampleRate,
                                                   status_t *status,
                                                   AudioSystem::audio_in_acoustics acoustics);
            virtual void            closeInputStream(AudioStreamIn* in);

            static uint32_t         checkInputSampleRate(uint32_t sampleRate);
            static const uint32_t   inputSamplingRates[];

                   int              mode() { return mMode; }
                   Mutex&           lock() { return mLock; }

                   int              setVoiceRecordGain(bool enable);
                   int              setVoiceRecordGain_l(bool enable);

        protected:
            /**
             * doRouting actually initiates the routing. A call to setRouting
             * or setMode may result in a routing change. The generic logic calls
             * doRouting when required. If the device has any special requirements these
             * methods can be overriden.
             */
                    status_t        doRouting(uint32_t device, bool force = false);
                    status_t        doRouting_l(uint32_t device, bool force = false);

            virtual status_t        dump(int fd, const Vector<String16>& args);

            friend class AudioStreamOutALSA;
            friend class AudioStreamInALSA;

            ALSAMixer              *mMixer;
            AudioStreamOutALSA     *mOutput;
            AudioStreamInALSA      *mInput;

        private:
            Mutex                   mLock;
            void                   *mSecRilLibHandle;
            HRilClient              mRilClient;
            bool                    mVrModeEnabled;

            HRilClient (*openClientRILD)  (void);
            int        (*disconnectRILD)  (HRilClient);
            int        (*closeClientRILD) (HRilClient);
            int        (*isConnectedRILD) (HRilClient);
            int        (*connectRILD)     (HRilClient);
            int        (*setCallVolume)   (HRilClient, SoundType, int);
            int        (*setCallAudioPath)(HRilClient, AudioPath);

            void                    loadRILD(void);
            status_t                connectRILDIfRequired(void);

    };

};        // namespace android
#endif    // ANDROID_AUDIO_HARDWARE_ALSA_H
