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

#define LOG_TAG "AudioPolicyManager"
//#define LOG_NDEBUG 0
#include <utils/Log.h>
#include "AudioPolicyManager.h"
#include <media/mediarecorder.h>

namespace android {


// ----------------------------------------------------------------------------
// AudioPolicyInterface implementation
// ----------------------------------------------------------------------------


status_t AudioPolicyManager::setDeviceConnectionState(AudioSystem::audio_devices device,
                                                  AudioSystem::device_connection_state state,
                                                  const char *device_address)
{

    LOGV("setDeviceConnectionState() device: %x, state %d, address %s", device, state, device_address);

    // connect/disconnect only 1 device at a time
    if (AudioSystem::popCount(device) != 1) return BAD_VALUE;

    if (strlen(device_address) >= MAX_DEVICE_ADDRESS_LEN) {
        LOGE("setDeviceConnectionState() invalid address: %s", device_address);
        return BAD_VALUE;
    }

    // handle output devices
    if (AudioSystem::isOutputDevice(device)) {

#ifndef WITH_A2DP
        if (AudioSystem::isA2dpDevice(device)) {
            LOGE("setDeviceConnectionState() invalid device: %x", device);
            return BAD_VALUE;
        }
#endif

        switch (state)
        {
        // handle output device connection
        case AudioSystem::DEVICE_STATE_AVAILABLE:
            if (mAvailableOutputDevices & device) {
                LOGW("setDeviceConnectionState() device already connected: %x", device);
                return INVALID_OPERATION;
            }
            LOGW_IF((getOutputForDevice((uint32_t)device) != 0), "setDeviceConnectionState(): output using unconnected device %x", device);

            LOGV("setDeviceConnectionState() connecting device %x", device);

            // register new device as available
            mAvailableOutputDevices |= device;

#ifdef WITH_A2DP
            // handle A2DP device connection
            if (AudioSystem::isA2dpDevice(device)) {
                // when an A2DP device is connected, open an A2DP and a duplicated output
                LOGV("opening A2DP output for device %s", device_address);
                AudioOutputDescriptor *outputDesc = new AudioOutputDescriptor();
                outputDesc->mDevice = device;
                mA2dpOutput = mpClientInterface->openOutput(&outputDesc->mDevice,
                                                        &outputDesc->mSamplingRate,
                                                        &outputDesc->mFormat,
                                                        &outputDesc->mChannels,
                                                        &outputDesc->mLatency,
                                                        outputDesc->mFlags);
                if (mA2dpOutput) {
                    // add A2DP output descriptor
                    mOutputs.add(mA2dpOutput, outputDesc);
                    // set initial stream volume for A2DP device
                    applyStreamVolumes(mA2dpOutput, device);
                    mDuplicatedOutput = mpClientInterface->openDuplicateOutput(mA2dpOutput, mHardwareOutput);
                    if (mDuplicatedOutput != 0) {
                        // If both A2DP and duplicated outputs are open, send device address to A2DP hardware
                        // interface
                        AudioParameter param;
                        param.add(String8("a2dp_sink_address"), String8(device_address));
                        mpClientInterface->setParameters(mA2dpOutput, param.toString());
                        mA2dpDeviceAddress = String8(device_address, MAX_DEVICE_ADDRESS_LEN);

                        // add duplicated output descriptor
                        AudioOutputDescriptor *dupOutputDesc = new AudioOutputDescriptor();
                        dupOutputDesc->mOutput1 = mOutputs.valueFor(mHardwareOutput);
                        dupOutputDesc->mOutput2 = mOutputs.valueFor(mA2dpOutput);
                        dupOutputDesc->mSamplingRate = outputDesc->mSamplingRate;
                        dupOutputDesc->mFormat = outputDesc->mFormat;
                        dupOutputDesc->mChannels = outputDesc->mChannels;
                        dupOutputDesc->mLatency = outputDesc->mLatency;
                        mOutputs.add(mDuplicatedOutput, dupOutputDesc);
                        applyStreamVolumes(mDuplicatedOutput, device);
                    } else {
                        LOGW("getOutput() could not open duplicated output for %d and %d",
                                mHardwareOutput, mA2dpOutput);
                        mAvailableOutputDevices &= ~device;
                        delete outputDesc;
                        return NO_INIT;
                    }
                } else {
                    LOGW("setDeviceConnectionState() could not open A2DP output for device %x", device);
                    mAvailableOutputDevices &= ~device;
                    delete outputDesc;
                    return NO_INIT;
                }
                AudioOutputDescriptor *hwOutputDesc = mOutputs.valueFor(mHardwareOutput);

                if (mA2dpDeviceAddress == mScoDeviceAddress) {
                    // It is normal to suspend twice if we are both in call,
                    // and have the hardware audio output routed to BT SCO
                    if (mPhoneState != AudioSystem::MODE_NORMAL) {
                        mpClientInterface->suspendOutput(mA2dpOutput);
                    }
                    if (AudioSystem::isBluetoothScoDevice((AudioSystem::audio_devices)hwOutputDesc->device())) {
                        mpClientInterface->suspendOutput(mA2dpOutput);
                    }
                }

                // move streams pertaining to STRATEGY_MEDIA to the newly opened A2DP output
                if (getDeviceForStrategy(STRATEGY_MEDIA) & device) {
                    for (int i = 0; i < (int)AudioSystem::NUM_STREAM_TYPES; i++) {
                        if (getStrategy((AudioSystem::stream_type)i) == STRATEGY_MEDIA) {
                            mpClientInterface->setStreamOutput((AudioSystem::stream_type)i, mA2dpOutput);
                            outputDesc->mRefCount[i] = hwOutputDesc->mRefCount[i];
                            hwOutputDesc->mRefCount[i] = 0;
                        }
                    }

                }
                // move streams pertaining to STRATEGY_DTMF to the newly opened A2DP output
                if (getDeviceForStrategy(STRATEGY_DTMF) & device) {
                    for (int i = 0; i < (int)AudioSystem::NUM_STREAM_TYPES; i++) {
                        if (getStrategy((AudioSystem::stream_type)i) == STRATEGY_DTMF) {
                            mpClientInterface->setStreamOutput((AudioSystem::stream_type)i, mA2dpOutput);
                            outputDesc->mRefCount[i] = hwOutputDesc->mRefCount[i];
                            hwOutputDesc->mRefCount[i] = 0;
                        }
                    }

                }
                // move streams pertaining to STRATEGY_SONIFICATION to the newly opened duplicated output
                if (getDeviceForStrategy(STRATEGY_SONIFICATION) & device) {
                    for (int i = 0; i < (int)AudioSystem::NUM_STREAM_TYPES; i++) {
                        if (getStrategy((AudioSystem::stream_type)i) == STRATEGY_SONIFICATION) {
                            mpClientInterface->setStreamOutput((AudioSystem::stream_type)i, mDuplicatedOutput);
                            outputDesc->mRefCount[i] =
                                hwOutputDesc->mRefCount[i];
                            mOutputs.valueFor(mDuplicatedOutput)->mRefCount[i] =
                                hwOutputDesc->mRefCount[i];
                        }
                    }
                }
            } else
#endif
            // handle wired and SCO device connection (accessed via hardware output)
            {

                uint32_t newDevice = 0;
                if (AudioSystem::isBluetoothScoDevice(device)) {
                    LOGV("setDeviceConnectionState() BT SCO  device, address %s", device_address);
                    // keep track of SCO device address
                    mScoDeviceAddress = String8(device_address, MAX_DEVICE_ADDRESS_LEN);
                    // if in call and connecting SCO device, check if we must reroute hardware output
                    if (mPhoneState == AudioSystem::MODE_IN_CALL &&
                        getDeviceForStrategy(STRATEGY_PHONE) == device) {
                        newDevice = device;
                    } else if (mOutputs.valueFor(mHardwareOutput)->isUsedByStrategy(STRATEGY_DTMF) &&
                               getDeviceForStrategy(STRATEGY_DTMF) == device) {
                        newDevice = device;
                    }
                    if ((mA2dpDeviceAddress == mScoDeviceAddress) &&
                        (mPhoneState != AudioSystem::MODE_NORMAL)) {
                        mpClientInterface->suspendOutput(mA2dpOutput);
                    }
                } else if (device == AudioSystem::DEVICE_OUT_WIRED_HEADSET ||
                           device == AudioSystem::DEVICE_OUT_WIRED_HEADPHONE) {
                    LOGV("setDeviceConnectionState() wired headset device");
                    // if connecting a wired headset, we check the following by order of priority
                    // to request a routing change if necessary:
                    // 1: we are in call or the strategy phone is active on the hardware output:
                    //      use device for strategy phone
                    // 2: the strategy sonification is active on the hardware output:
                    //      use device for strategy sonification
                    // 3: the strategy media is active on the hardware output:
                    //      use device for strategy media
                    // 4: the strategy DTMF is active on the hardware output:
                    //      use device for strategy DTMF
                    if (getDeviceForStrategy(STRATEGY_PHONE) == device &&
                        (mPhoneState == AudioSystem::MODE_IN_CALL ||
                        mOutputs.valueFor(mHardwareOutput)->isUsedByStrategy(STRATEGY_PHONE))) {
                        newDevice = device;
                    } else if ((getDeviceForStrategy(STRATEGY_SONIFICATION) & device) &&
                               mOutputs.valueFor(mHardwareOutput)->isUsedByStrategy(STRATEGY_SONIFICATION)){
                        newDevice = getDeviceForStrategy(STRATEGY_SONIFICATION);
                    } else if ((getDeviceForStrategy(STRATEGY_MEDIA) == device) &&
                               mOutputs.valueFor(mHardwareOutput)->isUsedByStrategy(STRATEGY_MEDIA)){
                        newDevice = device;
                    } else if (getDeviceForStrategy(STRATEGY_DTMF) == device &&
                            mOutputs.valueFor(mHardwareOutput)->isUsedByStrategy(STRATEGY_DTMF)) {
                         newDevice = device;
                    }
                }
                // request routing change if necessary
                setOutputDevice(mHardwareOutput, newDevice);
            }
            break;
        // handle output device disconnection
        case AudioSystem::DEVICE_STATE_UNAVAILABLE: {
            if (!(mAvailableOutputDevices & device)) {
                LOGW("setDeviceConnectionState() device not connected: %x", device);
                return INVALID_OPERATION;
            }

            uint32_t newDevice = 0;
            // get usage of disconnected device by all strategies
            bool wasUsedForMedia = (getDeviceForStrategy(STRATEGY_MEDIA) & device) != 0;
            bool wasUsedForSonification = (getDeviceForStrategy(STRATEGY_SONIFICATION) & device) != 0;
            bool wasUsedforPhone = (getDeviceForStrategy(STRATEGY_PHONE) & device) != 0;
            bool wasUsedforDtmf = (getDeviceForStrategy(STRATEGY_DTMF) & device) != 0;
            LOGV("setDeviceConnectionState() disconnecting device %x used by media %d, sonification %d, phone %d",
                    device, wasUsedForMedia, wasUsedForSonification, wasUsedforPhone);
            // remove device from available output devices
            mAvailableOutputDevices &= ~device;

#ifdef WITH_A2DP
            // handle A2DP device disconnection
            if (AudioSystem::isA2dpDevice(device)) {
                if (mA2dpOutput == 0 || mDuplicatedOutput == 0) {
                    LOGW("setDeviceConnectionState() disconnecting A2DP and no A2DP output!");
                    mAvailableOutputDevices |= device;
                    return INVALID_OPERATION;
                }

                if (mA2dpDeviceAddress != device_address) {
                    LOGW("setDeviceConnectionState() disconnecting unknow A2DP sink address %s", device_address);
                    mAvailableOutputDevices |= device;
                    return INVALID_OPERATION;
                }

                AudioOutputDescriptor *hwOutputDesc = mOutputs.valueFor(mHardwareOutput);
                AudioOutputDescriptor *a2dpOutputDesc = mOutputs.valueFor(mA2dpOutput);

                // mute media during 2 seconds to avoid outputing sound on hardware output while music stream
                // is switched from A2DP output and before music is paused by music application
                setStrategyMute(STRATEGY_MEDIA, true, mHardwareOutput);
                setStrategyMute(STRATEGY_MEDIA, false, mHardwareOutput, 2000);

                // If the A2DP device was used by DTMF strategy, move all streams pertaining to DTMF strategy to
                // hardware output
                if (wasUsedforDtmf) {
                    for (int i = 0; i < (int)AudioSystem::NUM_STREAM_TYPES; i++) {
                        if (getStrategy((AudioSystem::stream_type)i) == STRATEGY_DTMF) {
                            mpClientInterface->setStreamOutput((AudioSystem::stream_type)i, mHardwareOutput);
                            hwOutputDesc->changeRefCount((AudioSystem::stream_type)i,
                                    a2dpOutputDesc->mRefCount[i]);
                        }
                    }
                    if (a2dpOutputDesc->isUsedByStrategy(STRATEGY_DTMF)) {
                        newDevice = getDeviceForStrategy(STRATEGY_DTMF);
                    }
                }

                // If the A2DP device was used by media strategy, move all streams pertaining to media strategy to
                // hardware output
                if (wasUsedForMedia) {
                    for (int i = 0; i < (int)AudioSystem::NUM_STREAM_TYPES; i++) {
                        if (getStrategy((AudioSystem::stream_type)i) == STRATEGY_MEDIA) {
                            mpClientInterface->setStreamOutput((AudioSystem::stream_type)i, mHardwareOutput);
                            hwOutputDesc->changeRefCount((AudioSystem::stream_type)i,
                                    a2dpOutputDesc->mRefCount[i]);
                        }
                    }
                    if (a2dpOutputDesc->isUsedByStrategy(STRATEGY_MEDIA)) {
                        newDevice = getDeviceForStrategy(STRATEGY_MEDIA);
                    }
                }

                // If the A2DP device was used by sonification strategy, move all streams pertaining to
                // sonification strategy to hardware output.
                // Note that newDevice is overwritten here giving sonification strategy a higher priority than
                // media strategy.
                if (wasUsedForSonification) {
                    for (int i = 0; i < (int)AudioSystem::NUM_STREAM_TYPES; i++) {
                        if (getStrategy((AudioSystem::stream_type)i) == STRATEGY_SONIFICATION) {
                            mpClientInterface->setStreamOutput((AudioSystem::stream_type)i, mHardwareOutput);
                        }
                    }
                    if (a2dpOutputDesc->isUsedByStrategy(STRATEGY_SONIFICATION)) {
                        newDevice = getDeviceForStrategy(STRATEGY_SONIFICATION);
                    }
                }

                // close A2DP and duplicated outputs
                AudioParameter param;
                param.add(String8("closing"), String8("true"));
                mpClientInterface->setParameters(mA2dpOutput, param.toString());

                LOGW("setDeviceConnectionState() closing A2DP and duplicated output!");
                mpClientInterface->closeOutput(mDuplicatedOutput);
                delete mOutputs.valueFor(mDuplicatedOutput);
                mOutputs.removeItem(mDuplicatedOutput);
                mDuplicatedOutput = 0;
                mpClientInterface->closeOutput(mA2dpOutput);
                delete mOutputs.valueFor(mA2dpOutput);
                mOutputs.removeItem(mA2dpOutput);
                mA2dpOutput = 0;
            } else
#endif
            {
                if (AudioSystem::isBluetoothScoDevice(device)) {
                    // handle SCO device disconnection
                    if (wasUsedforPhone &&
                        mPhoneState == AudioSystem::MODE_IN_CALL) {
                        // if in call, find new suitable device for phone strategy
                        newDevice = getDeviceForStrategy(STRATEGY_PHONE);
                    } else if (wasUsedforDtmf &&
                               mOutputs.valueFor(mHardwareOutput)->isUsedByStrategy(STRATEGY_DTMF)) {
                        newDevice = getDeviceForStrategy(STRATEGY_DTMF);
                    }
                    if ((mA2dpDeviceAddress == mScoDeviceAddress) &&
                        (mPhoneState != AudioSystem::MODE_NORMAL)) {
                        mpClientInterface->restoreOutput(mA2dpOutput);
                    }
                } else if (device == AudioSystem::DEVICE_OUT_WIRED_HEADSET ||
                           device == AudioSystem::DEVICE_OUT_WIRED_HEADPHONE) {
                    // if disconnecting a wired headset, we check the following by order of priority
                    // to request a routing change if necessary:
                    // 1: we are in call or the strategy phone is active on the hardware output:
                    //      use device for strategy phone
                    // 2: the strategy sonification is active on the hardware output:
                    //      use device for strategy sonification
                    // 3: the strategy media is active on the hardware output:
                    //      use device for strategy media
                    // 4: the strategy DTMF is active on the hardware output:
                    //      use device for strategy DTMF
                    if (wasUsedforPhone &&
                        (mPhoneState == AudioSystem::MODE_IN_CALL ||
                         mOutputs.valueFor(mHardwareOutput)->isUsedByStrategy(STRATEGY_PHONE))) {
                        newDevice = getDeviceForStrategy(STRATEGY_PHONE);
                    } else if (wasUsedForSonification &&
                               mOutputs.valueFor(mHardwareOutput)->isUsedByStrategy(STRATEGY_SONIFICATION)){
                        newDevice = getDeviceForStrategy(STRATEGY_SONIFICATION);
                    } else if (wasUsedForMedia &&
                               mOutputs.valueFor(mHardwareOutput)->isUsedByStrategy(STRATEGY_MEDIA)){
                        newDevice = getDeviceForStrategy(STRATEGY_MEDIA);
                    } else if (wasUsedforDtmf &&
                               mOutputs.valueFor(mHardwareOutput)->isUsedByStrategy(STRATEGY_DTMF)){
                        newDevice = getDeviceForStrategy(STRATEGY_DTMF);
                    }
                }
            }
            // request routing change if necessary
            setOutputDevice(mHardwareOutput, newDevice);

            // clear A2DP and SCO device address if necessary
#ifdef WITH_A2DP
            if (AudioSystem::isA2dpDevice(device)) {
                mA2dpDeviceAddress = "";
            }
#endif
            if (AudioSystem::isBluetoothScoDevice(device)) {
                mScoDeviceAddress = "";
            }
            } break;

        default:
            LOGE("setDeviceConnectionState() invalid state: %x", state);
            return BAD_VALUE;
        }

        if (device == AudioSystem::DEVICE_OUT_WIRED_HEADSET) {
            device = AudioSystem::DEVICE_IN_WIRED_HEADSET;
        } else if (device == AudioSystem::DEVICE_OUT_BLUETOOTH_SCO ||
                   device == AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_HEADSET ||
                   device == AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_CARKIT) {
            device = AudioSystem::DEVICE_IN_BLUETOOTH_SCO_HEADSET;
        } else {
            return NO_ERROR;
        }
    }
    // handle input devices
    if (AudioSystem::isInputDevice(device)) {

        switch (state)
        {
        // handle input device connection
        case AudioSystem::DEVICE_STATE_AVAILABLE: {
            if (mAvailableInputDevices & device) {
                LOGW("setDeviceConnectionState() device already connected: %d", device);
                return INVALID_OPERATION;
            }
            mAvailableInputDevices |= device;
            }
            break;

        // handle input device disconnection
        case AudioSystem::DEVICE_STATE_UNAVAILABLE: {
            if (!(mAvailableInputDevices & device)) {
                LOGW("setDeviceConnectionState() device not connected: %d", device);
                return INVALID_OPERATION;
            }
            mAvailableInputDevices &= ~device;
            } break;

        default:
            LOGE("setDeviceConnectionState() invalid state: %x", state);
            return BAD_VALUE;
        }

        audio_io_handle_t activeInput = getActiveInput();
        if (activeInput != 0) {
            AudioInputDescriptor *inputDesc = mInputs.valueFor(activeInput);
            uint32_t newDevice = getDeviceForInputSource(inputDesc->mInputSource);
            if (newDevice != inputDesc->mDevice) {
                LOGV("setDeviceConnectionState() changing device from %x to %x for input %d",
                        inputDesc->mDevice, newDevice, activeInput);
                inputDesc->mDevice = newDevice;
                AudioParameter param = AudioParameter();
                param.addInt(String8(AudioParameter::keyRouting), (int)newDevice);
                mpClientInterface->setParameters(activeInput, param.toString());
            }
        }

        return NO_ERROR;
    }

    LOGW("setDeviceConnectionState() invalid device: %x", device);
    return BAD_VALUE;
}

AudioSystem::device_connection_state AudioPolicyManager::getDeviceConnectionState(AudioSystem::audio_devices device,
                                                  const char *device_address)
{
    AudioSystem::device_connection_state state = AudioSystem::DEVICE_STATE_UNAVAILABLE;
    String8 address = String8(device_address);
    if (AudioSystem::isOutputDevice(device)) {
        if (device & mAvailableOutputDevices) {
#ifdef WITH_A2DP
            if (AudioSystem::isA2dpDevice(device) &&
                address != "" && mA2dpDeviceAddress != address) {
                return state;
            }
#endif
            if (AudioSystem::isBluetoothScoDevice(device) &&
                address != "" && mScoDeviceAddress != address) {
                return state;
            }
            state = AudioSystem::DEVICE_STATE_AVAILABLE;
        }
    } else if (AudioSystem::isInputDevice(device)) {
        if (device & mAvailableInputDevices) {
            state = AudioSystem::DEVICE_STATE_AVAILABLE;
        }
    }

    return state;
}

void AudioPolicyManager::setPhoneState(int state)
{
    LOGV("setPhoneState() state %d", state);
    uint32_t newDevice = 0;
    if (state < 0 || state >= AudioSystem::NUM_MODES) {
        LOGW("setPhoneState() invalid state %d", state);
        return;
    }

    if (state == mPhoneState ) {
        LOGW("setPhoneState() setting same state %d", state);
        return;
    }

    // if leaving call state, handle special case of active streams
    // pertaining to sonification strategy see handleIncallSonification()
    if (mPhoneState == AudioSystem::MODE_IN_CALL) {
        LOGV("setPhoneState() in call state management: new state is %d", state);
        for (int stream = 0; stream < AudioSystem::NUM_STREAM_TYPES; stream++) {
            handleIncallSonification(stream, false, true);
        }
    }

    // store previous phone state for management of sonification strategy below
    int oldState = mPhoneState;
    uint32_t oldDtmfDevice = getDeviceForStrategy(STRATEGY_DTMF);
    uint32_t oldSonificationDevice = getDeviceForStrategy(STRATEGY_SONIFICATION) & ~AudioSystem::DEVICE_OUT_SPEAKER;
    mPhoneState = state;
    bool force = false;
    AudioOutputDescriptor *hwOutputDesc = mOutputs.valueFor(mHardwareOutput);
    // check if a routing change is required for hardware output in the following
    // order of priority:
    // 1: a stream pertaining to sonification strategy is active
    // 2: new state is incall
    // 3: a stream pertaining to media strategy is active
    // 4: a stream pertaining to DTMF strategy is active
    if (hwOutputDesc->isUsedByStrategy(STRATEGY_SONIFICATION)) {
        newDevice = getDeviceForStrategy(STRATEGY_SONIFICATION);
    } else if (mPhoneState == AudioSystem::MODE_IN_CALL) {
        newDevice = getDeviceForStrategy(STRATEGY_PHONE);
        // force routing command to audio hardware when starting call
        // even if no device change is needed
        force = true;
    } else if (hwOutputDesc->isUsedByStrategy(STRATEGY_MEDIA)) {
        newDevice = getDeviceForStrategy(STRATEGY_MEDIA);
    } else if (hwOutputDesc->isUsedByStrategy(STRATEGY_DTMF)) {
        newDevice = getDeviceForStrategy(STRATEGY_DTMF);
    }


    if (mA2dpOutput != 0) {
        // If entering or exiting in call state, switch DTMF streams to/from A2DP output
        // if necessary
        uint32_t newDtmfDevice = getDeviceForStrategy(STRATEGY_DTMF);
        uint32_t newSonificationDevice = getDeviceForStrategy(STRATEGY_SONIFICATION) & ~AudioSystem::DEVICE_OUT_SPEAKER;
        if (state == AudioSystem::MODE_IN_CALL) {  // entering in call mode
            // move DTMF streams from A2DP output to hardware output if necessary
            if (AudioSystem::isA2dpDevice((AudioSystem::audio_devices)oldDtmfDevice) &&
                !AudioSystem::isA2dpDevice((AudioSystem::audio_devices)newDtmfDevice)) {
                for (int i = 0; i < (int)AudioSystem::NUM_STREAM_TYPES; i++) {
                    if (getStrategy((AudioSystem::stream_type)i) == STRATEGY_DTMF) {
                        mpClientInterface->setStreamOutput((AudioSystem::stream_type)i, mHardwareOutput);
                        int refCount = mOutputs.valueFor(mA2dpOutput)->mRefCount[i];
                        hwOutputDesc->changeRefCount((AudioSystem::stream_type)i,
                                refCount);
                        mOutputs.valueFor(mA2dpOutput)->changeRefCount((AudioSystem::stream_type)i,-refCount);
                    }
                }
                if (newDevice == 0 && mOutputs.valueFor(mA2dpOutput)->isUsedByStrategy(STRATEGY_DTMF)) {
                    newDevice = newDtmfDevice;
                }
            }
            // move SONIFICATION streams from duplicated output to hardware output if necessary
            if (AudioSystem::isA2dpDevice((AudioSystem::audio_devices)oldSonificationDevice) &&
                !AudioSystem::isA2dpDevice((AudioSystem::audio_devices)newSonificationDevice)) {
                for (int i = 0; i < (int)AudioSystem::NUM_STREAM_TYPES; i++) {
                    if (getStrategy((AudioSystem::stream_type)i) == STRATEGY_SONIFICATION) {
                        mpClientInterface->setStreamOutput((AudioSystem::stream_type)i, mHardwareOutput);
                        int refCount = mOutputs.valueFor(mDuplicatedOutput)->mRefCount[i];
                        hwOutputDesc->changeRefCount((AudioSystem::stream_type)i,
                                refCount);
                        mOutputs.valueFor(mDuplicatedOutput)->changeRefCount((AudioSystem::stream_type)i,-refCount);
                    }
                }
            }
        } else {  // exiting in call mode
            // move DTMF streams from hardware output to A2DP output if necessary
            if (!AudioSystem::isA2dpDevice((AudioSystem::audio_devices)oldDtmfDevice) &&
                AudioSystem::isA2dpDevice((AudioSystem::audio_devices)newDtmfDevice)) {
                for (int i = 0; i < (int)AudioSystem::NUM_STREAM_TYPES; i++) {
                    if (getStrategy((AudioSystem::stream_type)i) == STRATEGY_DTMF) {
                        mpClientInterface->setStreamOutput((AudioSystem::stream_type)i, mA2dpOutput);
                        int refCount = hwOutputDesc->mRefCount[i];
                        mOutputs.valueFor(mA2dpOutput)->changeRefCount((AudioSystem::stream_type)i, refCount);
                        hwOutputDesc->changeRefCount((AudioSystem::stream_type)i, -refCount);
                    }
                }
            }
            // move SONIFICATION streams from hardware output to A2DP output if necessary
            if (!AudioSystem::isA2dpDevice((AudioSystem::audio_devices)oldSonificationDevice) &&
                AudioSystem::isA2dpDevice((AudioSystem::audio_devices)newSonificationDevice)) {
                for (int i = 0; i < (int)AudioSystem::NUM_STREAM_TYPES; i++) {
                    if (getStrategy((AudioSystem::stream_type)i) == STRATEGY_SONIFICATION) {
                        mpClientInterface->setStreamOutput((AudioSystem::stream_type)i, mDuplicatedOutput);
                        int refCount = hwOutputDesc->mRefCount[i];
                        mOutputs.valueFor(mDuplicatedOutput)->changeRefCount((AudioSystem::stream_type)i, refCount);
                        hwOutputDesc->changeRefCount((AudioSystem::stream_type)i, -refCount);
                    }
                }
            }
        }
        // suspend A2DP output if SCO device address is the same as A2DP device address.
        // no need to check that a SCO device is actually connected as mScoDeviceAddress == ""
        // if none is connected and the test below will fail.
        if (mA2dpDeviceAddress == mScoDeviceAddress) {
            if (oldState == AudioSystem::MODE_NORMAL) {
                mpClientInterface->suspendOutput(mA2dpOutput);
            } else if (state == AudioSystem::MODE_NORMAL) {
                mpClientInterface->restoreOutput(mA2dpOutput);
            }
        }
    }

    // force routing command to audio hardware when ending call
    // even if no device change is needed
    if (oldState == AudioSystem::MODE_IN_CALL) {
        if (newDevice == 0) {
            newDevice = hwOutputDesc->device();
        }
        force = true;
    }
    // change routing is necessary
    setOutputDevice(mHardwareOutput, newDevice, force);

    // if entering in call state, handle special case of active streams
    // pertaining to sonification strategy see handleIncallSonification()
    if (state == AudioSystem::MODE_IN_CALL) {
        LOGV("setPhoneState() in call state management: new state is %d", state);
        for (int stream = 0; stream < AudioSystem::NUM_STREAM_TYPES; stream++) {
            handleIncallSonification(stream, true, true);
        }
    }

    // Flag that ringtone volume must be limited to music volume until we exit MODE_RINGTONE
    if (state == AudioSystem::MODE_RINGTONE &&
        (hwOutputDesc->isUsedByStream(AudioSystem::MUSIC) ||
        (systemTime() - mMusicStopTime) < seconds(SONIFICATION_HEADSET_MUSIC_DELAY))) {
        mLimitRingtoneVolume = true;
    } else {
        mLimitRingtoneVolume = false;
    }
}

void AudioPolicyManager::setRingerMode(uint32_t mode, uint32_t mask)
{
    LOGV("setRingerMode() mode %x, mask %x", mode, mask);

    mRingerMode = mode;
}

void AudioPolicyManager::setForceUse(AudioSystem::force_use usage, AudioSystem::forced_config config)
{
    LOGV("setForceUse() usage %d, config %d, mPhoneState %d", usage, config, mPhoneState);

    switch(usage) {
    case AudioSystem::FOR_COMMUNICATION:
        if (config != AudioSystem::FORCE_SPEAKER && config != AudioSystem::FORCE_BT_SCO &&
            config != AudioSystem::FORCE_NONE) {
            LOGW("setForceUse() invalid config %d for FOR_COMMUNICATION", config);
            return;
        }
        mForceUse[usage] = config;
        // update hardware output routing immediately if in call, or if there is an active
        // VOICE_CALL stream, as would be the case with an application that uses this stream
        // for it to behave like in a telephony app (e.g. voicemail app that plays audio files
        // streamed or downloaded to the device)
        if ((mPhoneState == AudioSystem::MODE_IN_CALL) ||
                (mOutputs.valueFor(mHardwareOutput)->isUsedByStream(AudioSystem::VOICE_CALL))) {
            uint32_t device = getDeviceForStrategy(STRATEGY_PHONE);
            setOutputDevice(mHardwareOutput, device);
        }
        break;
    case AudioSystem::FOR_MEDIA:
        if (config != AudioSystem::FORCE_HEADPHONES && config != AudioSystem::FORCE_BT_A2DP &&
            config != AudioSystem::FORCE_WIRED_ACCESSORY && config != AudioSystem::FORCE_NONE) {
            LOGW("setForceUse() invalid config %d for FOR_MEDIA", config);
            return;
        }
        mForceUse[usage] = config;
        break;
    case AudioSystem::FOR_RECORD:
        if (config != AudioSystem::FORCE_BT_SCO && config != AudioSystem::FORCE_WIRED_ACCESSORY &&
            config != AudioSystem::FORCE_NONE) {
            LOGW("setForceUse() invalid config %d for FOR_RECORD", config);
            return;
        }
        mForceUse[usage] = config;
        break;
    case AudioSystem::FOR_DOCK:
        if (config != AudioSystem::FORCE_NONE && config != AudioSystem::FORCE_BT_CAR_DOCK &&
            config != AudioSystem::FORCE_BT_DESK_DOCK && config != AudioSystem::FORCE_WIRED_ACCESSORY) {
            LOGW("setForceUse() invalid config %d for FOR_DOCK", config);
        }
        mForceUse[usage] = config;
        break;
    default:
        LOGW("setForceUse() invalid usage %d", usage);
        break;
    }
}

AudioSystem::forced_config AudioPolicyManager::getForceUse(AudioSystem::force_use usage)
{
    return mForceUse[usage];
}

void AudioPolicyManager::setSystemProperty(const char* property, const char* value)
{
    LOGV("setSystemProperty() property %s, value %s", property, value);
    if (strcmp(property, "ro.camera.sound.forced") == 0) {
        if (atoi(value)) {
            LOGV("ENFORCED_AUDIBLE cannot be muted");
            mStreams[AudioSystem::ENFORCED_AUDIBLE].mCanBeMuted = false;
        } else {
            LOGV("ENFORCED_AUDIBLE can be muted");
            mStreams[AudioSystem::ENFORCED_AUDIBLE].mCanBeMuted = true;
        }
    }
}

audio_io_handle_t AudioPolicyManager::getOutput(AudioSystem::stream_type stream,
                                    uint32_t samplingRate,
                                    uint32_t format,
                                    uint32_t channels,
                                    AudioSystem::output_flags flags)
{
    audio_io_handle_t output = 0;
    uint32_t latency = 0;
    routing_strategy strategy = getStrategy((AudioSystem::stream_type)stream);
    uint32_t device = getDeviceForStrategy(strategy);
    LOGV("getOutput() stream %d, samplingRate %d, format %d, channels %x, flags %x", stream, samplingRate, format, channels, flags);


    // open a direct output if:
    // 1 a direct output is explicitely requested
    // 2 the audio format is compressed
    if ((flags & AudioSystem::OUTPUT_FLAG_DIRECT) ||
         (format !=0 && !AudioSystem::isLinearPCM(format))) {

        LOGV("getOutput() opening direct output device %x", device);
        AudioOutputDescriptor *outputDesc = new AudioOutputDescriptor();
        outputDesc->mDevice = device;
        outputDesc->mSamplingRate = samplingRate;
        outputDesc->mFormat = format;
        outputDesc->mChannels = channels;
        outputDesc->mLatency = 0;
        outputDesc->mFlags = (AudioSystem::output_flags)(flags | AudioSystem::OUTPUT_FLAG_DIRECT);
        outputDesc->mRefCount[stream] = 1;
        output = mpClientInterface->openOutput(&outputDesc->mDevice,
                                        &outputDesc->mSamplingRate,
                                        &outputDesc->mFormat,
                                        &outputDesc->mChannels,
                                        &outputDesc->mLatency,
                                        outputDesc->mFlags);

        // only accept an output with the requeted parameters
        if ((samplingRate != 0 && samplingRate != outputDesc->mSamplingRate) ||
            (format != 0 && format != outputDesc->mFormat) ||
            (channels != 0 && channels != outputDesc->mChannels)) {
            LOGV("getOutput() failed opening direct output: samplingRate %d, format %d, channels %d",
                    samplingRate, format, channels);
            mpClientInterface->closeOutput(output);
            delete outputDesc;
            return 0;
        }
        mOutputs.add(output, outputDesc);
        return output;
    }

    if (channels != 0 && channels != AudioSystem::CHANNEL_OUT_MONO &&
        channels != AudioSystem::CHANNEL_OUT_STEREO) {
        return 0;
    }
    // open a non direct output

    // get which output is suitable for the specified stream. The actual routing change will happen
    // when startOutput() will be called
    uint32_t device2 = device & ~AudioSystem::DEVICE_OUT_SPEAKER;
    if (AudioSystem::popCount((AudioSystem::audio_devices)device) == 2) {
#ifdef WITH_A2DP
        if (AudioSystem::isA2dpDevice((AudioSystem::audio_devices)device2)) {
            // if playing on 2 devices among which one is A2DP, use duplicated output
            LOGV("getOutput() using duplicated output");
            LOGW_IF((mA2dpOutput == 0), "getOutput() A2DP device in multiple %x selected but A2DP output not opened", device);
            output = mDuplicatedOutput;
        } else
#endif
        {
            // if playing on 2 devices among which none is A2DP, use hardware output
            output = mHardwareOutput;
        }
        LOGV("getOutput() using output %d for 2 devices %x", output, device);
    } else {
#ifdef WITH_A2DP
        if (AudioSystem::isA2dpDevice((AudioSystem::audio_devices)device2)) {
            // if playing on A2DP device, use a2dp output
            LOGW_IF((mA2dpOutput == 0), "getOutput() A2DP device %x selected but A2DP output not opened", device);
            output = mA2dpOutput;
        } else
#endif
        {
            // if playing on not A2DP device, use hardware output
            output = mHardwareOutput;
        }
    }


    LOGW_IF((output ==0), "getOutput() could not find output for stream %d, samplingRate %d, format %d, channels %x, flags %x",
                stream, samplingRate, format, channels, flags);

    return output;
}

status_t AudioPolicyManager::startOutput(audio_io_handle_t output, AudioSystem::stream_type stream,int session)
{
    LOGV("startOutput() output %d, stream %d", output, stream);
    ssize_t index = mOutputs.indexOfKey(output);
    if (index < 0) {
        LOGW("startOutput() unknow output %d", output);
        return BAD_VALUE;
    }

    AudioOutputDescriptor *outputDesc = mOutputs.valueAt(index);
    routing_strategy strategy = getStrategy((AudioSystem::stream_type)stream);
    uint32_t device = getDeviceForStrategy(strategy);

    if (!outputDesc->isUsedByStrategy(strategy)) {
        // if the stream started is the first active stream in its strategy, check if routing change
        // must be done on hardware output
        uint32_t newDevice = 0;
        if (AudioSystem::popCount((AudioSystem::audio_devices)device) == 2) {
#ifdef WITH_A2DP
            uint32_t device2 = device & ~AudioSystem::DEVICE_OUT_SPEAKER;
            if (AudioSystem::isA2dpDevice((AudioSystem::audio_devices)device2)) {
                // if one device is A2DP, selected the second device for hardware output
                device &= ~device2;
            } else
#endif
            {
                // we only support speaker + headset and speaker + headphone combinations on hardware output.
                // other combinations will leave device = 0 and no routing will happen.
                if (device != (AudioSystem::DEVICE_OUT_SPEAKER | AudioSystem::DEVICE_OUT_WIRED_HEADSET) &&
                    device != (AudioSystem::DEVICE_OUT_SPEAKER | AudioSystem::DEVICE_OUT_WIRED_HEADPHONE)) {
                    device = AudioSystem::DEVICE_OUT_SPEAKER;
                }
            }
        }

        // By order of priority
        // 1 apply routing for phone strategy in any case
        // 2 apply routing for notification strategy if no stream pertaining to
        //   phone strategies is playing
        // 3 apply routing for media strategy is not incall and neither phone nor sonification
        //   strategies is active.
        // 4 apply routing for DTMF strategy if no stream pertaining to
        //   neither phone, sonification nor media strategy is playing
        if (strategy == STRATEGY_PHONE) {
            newDevice = device;
        } else if (!mOutputs.valueFor(mHardwareOutput)->isUsedByStrategy(STRATEGY_PHONE)) {
            if (strategy == STRATEGY_SONIFICATION) {
                newDevice = device;
            } else if (!mOutputs.valueFor(mHardwareOutput)->isUsedByStrategy(STRATEGY_SONIFICATION)) {
                if (strategy == STRATEGY_MEDIA) {
                    newDevice = device;
                } else if (!mOutputs.valueFor(mHardwareOutput)->isUsedByStrategy(STRATEGY_MEDIA)) {
                    // strategy == STRATEGY_DTMF
                    newDevice = device;
                }
            }
        }

        // TODO: maybe mute stream is selected device was refused
        setOutputDevice(mHardwareOutput, newDevice);
    }

    // incremenent usage count for this stream on the requested output:
    // NOTE that the usage count is the same for duplicated output and hardware output which is
    // necassary for a correct control of hardware output routing by startOutput() and stopOutput()
    outputDesc->changeRefCount(stream, 1);

    // handle special case for sonification while in call
    if (mPhoneState == AudioSystem::MODE_IN_CALL) {
        handleIncallSonification(stream, true, false);
    }

    // apply volume rules for current stream and device if necessary
    checkAndSetVolume(stream, mStreams[stream].mIndexCur, output, outputDesc->device());

    return NO_ERROR;
}

status_t AudioPolicyManager::stopOutput(audio_io_handle_t output, AudioSystem::stream_type stream,int session)
{
    LOGV("stopOutput() output %d, stream %d", output, stream);
    ssize_t index = mOutputs.indexOfKey(output);
    if (index < 0) {
        LOGW("stopOutput() unknow output %d", output);
        return BAD_VALUE;
    }

    AudioOutputDescriptor *outputDesc = mOutputs.valueAt(index);
    routing_strategy strategy = getStrategy((AudioSystem::stream_type)stream);

    // handle special case for sonification while in call
    if (mPhoneState == AudioSystem::MODE_IN_CALL) {
        handleIncallSonification(stream, false, false);
    }

    if (outputDesc->isUsedByStrategy(strategy)) {
        // decrement usage count of this stream on the output
        outputDesc->changeRefCount(stream, -1);
        if (!outputDesc->isUsedByStrategy(strategy)) {
            // if the stream is the last of its strategy to use this output, change routing
            // in the following order or priority:
            // PHONE > SONIFICATION > MEDIA > DTMF
            uint32_t newDevice = 0;
            if (outputDesc->isUsedByStrategy(STRATEGY_PHONE)) {
                newDevice = getDeviceForStrategy(STRATEGY_PHONE);
            } else if (outputDesc->isUsedByStrategy(STRATEGY_SONIFICATION)) {
                newDevice = getDeviceForStrategy(STRATEGY_SONIFICATION);
            } else if (mPhoneState == AudioSystem::MODE_IN_CALL) {
                newDevice = getDeviceForStrategy(STRATEGY_PHONE);
            } else if (outputDesc->isUsedByStrategy(STRATEGY_MEDIA)) {
                newDevice = getDeviceForStrategy(STRATEGY_MEDIA);
            } else if (outputDesc->isUsedByStrategy(STRATEGY_DTMF)) {
                newDevice = getDeviceForStrategy(STRATEGY_DTMF);
            }

            // apply routing change if necessary.
            // insert a delay of 2 times the audio hardware latency to ensure PCM
            // buffers in audio flinger and audio hardware are emptied before the
            // routing change is executed.
            setOutputDevice(mHardwareOutput, newDevice, false, mOutputs.valueFor(mHardwareOutput)->mLatency*2);
        }
        // store time at which the last music track was stopped - see computeVolume()
        if (stream == AudioSystem::MUSIC) {
            mMusicStopTime = systemTime();
        }
        return NO_ERROR;
    } else {
        LOGW("stopOutput() refcount is already 0 for output %d", output);
        return INVALID_OPERATION;
    }
}

void AudioPolicyManager::releaseOutput(audio_io_handle_t output)
{
    LOGV("releaseOutput() %d", output);
    ssize_t index = mOutputs.indexOfKey(output);
    if (index < 0) {
        LOGW("releaseOutput() releasing unknown output %d", output);
        return;
    }
    if (mOutputs.valueAt(index)->mFlags & AudioSystem::OUTPUT_FLAG_DIRECT) {
        mpClientInterface->closeOutput(output);
        delete mOutputs.valueAt(index);
        mOutputs.removeItem(output);
    }
}

audio_io_handle_t AudioPolicyManager::getInput(int inputSource,
                                    uint32_t samplingRate,
                                    uint32_t format,
                                    uint32_t channels,
                                    AudioSystem::audio_in_acoustics acoustics)
{
    audio_io_handle_t input = 0;
    uint32_t device = getDeviceForInputSource(inputSource);

    LOGV("getInput() inputSource %d, samplingRate %d, format %d, channels %x, acoustics %x", inputSource, samplingRate, format, channels, acoustics);

    if (device == 0) {
        return 0;
    }

    // adapt channel selection to input source
    switch(inputSource) {
    case AUDIO_SOURCE_VOICE_UPLINK:
        channels = AudioSystem::CHANNEL_IN_VOICE_UPLINK;
        break;
    case AUDIO_SOURCE_VOICE_DOWNLINK:
        channels = AudioSystem::CHANNEL_IN_VOICE_DNLINK;
        break;
    case AUDIO_SOURCE_VOICE_CALL:
        channels = (AudioSystem::CHANNEL_IN_VOICE_UPLINK | AudioSystem::CHANNEL_IN_VOICE_DNLINK);
        break;
    default:
        break;
    }

    AudioInputDescriptor *inputDesc = new AudioInputDescriptor();

    inputDesc->mInputSource = inputSource;
    inputDesc->mDevice = device;
    inputDesc->mSamplingRate = samplingRate;
    inputDesc->mFormat = format;
    inputDesc->mChannels = channels;
    inputDesc->mAcoustics = acoustics;
    inputDesc->mRefCount = 0;
    input = mpClientInterface->openInput(&inputDesc->mDevice,
                                    &inputDesc->mSamplingRate,
                                    &inputDesc->mFormat,
                                    &inputDesc->mChannels,
                                    inputDesc->mAcoustics);

    // only accept input with the exact requested set of parameters
    if ((samplingRate != inputDesc->mSamplingRate) ||
        (format != inputDesc->mFormat) ||
        (channels != inputDesc->mChannels)) {
        LOGV("getOutput() failed opening input: samplingRate %d, format %d, channels %d",
                samplingRate, format, channels);
        mpClientInterface->closeInput(input);
        delete inputDesc;
        return 0;
    }
    mInputs.add(input, inputDesc);
    return input;
}

status_t AudioPolicyManager::startInput(audio_io_handle_t input)
{
    LOGV("startInput() input %d", input);
    ssize_t index = mInputs.indexOfKey(input);
    if (index < 0) {
        LOGW("startInput() unknow input %d", input);
        return BAD_VALUE;
    }
    AudioInputDescriptor *inputDesc = mInputs.valueAt(index);

    // refuse 2 active AudioRecord clients at the same time
    if (getActiveInput() != 0) {
        LOGW("startInput() input %d failed: other input already started", input);
        return INVALID_OPERATION;
    }

    AudioParameter param = AudioParameter();
    param.addInt(String8(AudioParameter::keyRouting), (int)inputDesc->mDevice);
    mpClientInterface->setParameters(input, param.toString());

    inputDesc->mRefCount = 1;
    return NO_ERROR;
}

status_t AudioPolicyManager::stopInput(audio_io_handle_t input)
{
    LOGV("stopInput() input %d", input);
    ssize_t index = mInputs.indexOfKey(input);
    if (index < 0) {
        LOGW("stopInput() unknow input %d", input);
        return BAD_VALUE;
    }
    AudioInputDescriptor *inputDesc = mInputs.valueAt(index);

    if (inputDesc->mRefCount == 0) {
        LOGW("stopInput() input %d already stopped", input);
        return INVALID_OPERATION;
    } else {
        AudioParameter param = AudioParameter();
        param.addInt(String8(AudioParameter::keyRouting), 0);
        mpClientInterface->setParameters(input, param.toString());
        inputDesc->mRefCount = 0;
        return NO_ERROR;
    }
}

void AudioPolicyManager::releaseInput(audio_io_handle_t input)
{
    LOGV("releaseInput() %d", input);
    ssize_t index = mInputs.indexOfKey(input);
    if (index < 0) {
        LOGW("releaseInput() releasing unknown input %d", input);
        return;
    }
    mpClientInterface->closeInput(input);
    delete mInputs.valueAt(index);
    mInputs.removeItem(input);
    LOGV("releaseInput() exit");
}



void AudioPolicyManager::initStreamVolume(AudioSystem::stream_type stream,
                                            int indexMin,
                                            int indexMax)
{
    LOGV("initStreamVolume() stream %d, min %d, max %d", stream , indexMin, indexMax);
    if (indexMin < 0 || indexMin >= indexMax) {
        LOGW("initStreamVolume() invalid index limits for stream %d, min %d, max %d", stream , indexMin, indexMax);
        return;
    }
    mStreams[stream].mIndexMin = indexMin;
    mStreams[stream].mIndexMax = indexMax;
}

status_t AudioPolicyManager::setStreamVolumeIndex(AudioSystem::stream_type stream, int index)
{

    if ((index < mStreams[stream].mIndexMin) || (index > mStreams[stream].mIndexMax)) {
        return BAD_VALUE;
    }

    // Force max volume if stream cannot be muted
    if (!mStreams[stream].mCanBeMuted) index = mStreams[stream].mIndexMax;

    LOGV("setStreamVolumeIndex() stream %d, index %d", stream, index);
    mStreams[stream].mIndexCur = index;

    // compute and apply stream volume on all outputs according to connected device
    status_t status = NO_ERROR;
    for (size_t i = 0; i < mOutputs.size(); i++) {
        status_t volStatus = checkAndSetVolume(stream, index, mOutputs.keyAt(i), mOutputs.valueAt(i)->device());
        if (volStatus != NO_ERROR) {
            status = volStatus;
        }
    }
    return status;
}

status_t AudioPolicyManager::getStreamVolumeIndex(AudioSystem::stream_type stream, int *index)
{
    if (index == 0) {
        return BAD_VALUE;
    }
    LOGV("getStreamVolumeIndex() stream %d", stream);
    *index =  mStreams[stream].mIndexCur;
    return NO_ERROR;
}

status_t AudioPolicyManager::dump(int fd)
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;

    snprintf(buffer, SIZE, "\nAudioPolicyManager Dump: %p\n", this);
    result.append(buffer);
    snprintf(buffer, SIZE, " Hardware Output: %d\n", mHardwareOutput);
    result.append(buffer);
    snprintf(buffer, SIZE, " A2DP Output: %d\n", mA2dpOutput);
    result.append(buffer);
    snprintf(buffer, SIZE, " Duplicated Output: %d\n", mDuplicatedOutput);
    result.append(buffer);
    snprintf(buffer, SIZE, " Output devices: %08x\n", mAvailableOutputDevices);
    result.append(buffer);
    snprintf(buffer, SIZE, " Input devices: %08x\n", mAvailableInputDevices);
    result.append(buffer);
    snprintf(buffer, SIZE, " A2DP device address: %s\n", mA2dpDeviceAddress.string());
    result.append(buffer);
    snprintf(buffer, SIZE, " SCO device address: %s\n", mScoDeviceAddress.string());
    result.append(buffer);
    snprintf(buffer, SIZE, " Phone state: %d\n", mPhoneState);
    result.append(buffer);
    snprintf(buffer, SIZE, " Ringer mode: %d\n", mRingerMode);
    result.append(buffer);
    snprintf(buffer, SIZE, " Force use for communications %d\n", mForceUse[AudioSystem::FOR_COMMUNICATION]);
    result.append(buffer);
    snprintf(buffer, SIZE, " Force use for media %d\n", mForceUse[AudioSystem::FOR_MEDIA]);
    result.append(buffer);
    snprintf(buffer, SIZE, " Force use for record %d\n", mForceUse[AudioSystem::FOR_RECORD]);
    result.append(buffer);
    snprintf(buffer, SIZE, " Force use for dock %d\n", mForceUse[AudioSystem::FOR_DOCK]);
    result.append(buffer);
    write(fd, result.string(), result.size());

    snprintf(buffer, SIZE, "\nOutputs dump:\n");
    write(fd, buffer, strlen(buffer));
    for (size_t i = 0; i < mOutputs.size(); i++) {
        snprintf(buffer, SIZE, "- Output %d dump:\n", mOutputs.keyAt(i));
        write(fd, buffer, strlen(buffer));
        mOutputs.valueAt(i)->dump(fd);
    }

    snprintf(buffer, SIZE, "\nInputs dump:\n");
    write(fd, buffer, strlen(buffer));
    for (size_t i = 0; i < mInputs.size(); i++) {
        snprintf(buffer, SIZE, "- Input %d dump:\n", mInputs.keyAt(i));
        write(fd, buffer, strlen(buffer));
        mInputs.valueAt(i)->dump(fd);
    }

    snprintf(buffer, SIZE, "\nStreams dump:\n");
    write(fd, buffer, strlen(buffer));
    snprintf(buffer, SIZE, " Stream  Index Min  Index Max  Index Cur  Mute Count  Can be muted\n");
    write(fd, buffer, strlen(buffer));
    for (size_t i = 0; i < AudioSystem::NUM_STREAM_TYPES; i++) {
        snprintf(buffer, SIZE, " %02d", i);
        mStreams[i].dump(buffer + 3, SIZE);
        write(fd, buffer, strlen(buffer));
    }

    return NO_ERROR;
}

// ----------------------------------------------------------------------------
// AudioPolicyManager
// ----------------------------------------------------------------------------

// ---  class factory


extern "C" AudioPolicyInterface* createAudioPolicyManager(AudioPolicyClientInterface *clientInterface)
{
    return new AudioPolicyManager(clientInterface);
}

extern "C" void destroyAudioPolicyManager(AudioPolicyInterface *interface)
{
    delete interface;
}

AudioPolicyManager::AudioPolicyManager(AudioPolicyClientInterface *clientInterface)
: mPhoneState(AudioSystem::MODE_NORMAL), mRingerMode(0), mMusicStopTime(0), mLimitRingtoneVolume(false),mTotalEffectsCpuLoad(0),mTotalEffectsMemory(0)
{
    mpClientInterface = clientInterface;

    for (int i = 0; i < AudioSystem::NUM_FORCE_USE; i++) {
        mForceUse[i] = AudioSystem::FORCE_NONE;
    }

    // devices available by default are speaker, ear piece and microphone
    mAvailableOutputDevices = AudioSystem::DEVICE_OUT_EARPIECE |
                        AudioSystem::DEVICE_OUT_SPEAKER;
    mAvailableInputDevices = AudioSystem::DEVICE_IN_BUILTIN_MIC;

    mA2dpDeviceAddress = String8("");
    mScoDeviceAddress = String8("");

    // open hardware output
    AudioOutputDescriptor *outputDesc = new AudioOutputDescriptor();
    outputDesc->mDevice = (uint32_t)AudioSystem::DEVICE_OUT_SPEAKER;
    mHardwareOutput = mpClientInterface->openOutput(&outputDesc->mDevice,
                                    &outputDesc->mSamplingRate,
                                    &outputDesc->mFormat,
                                    &outputDesc->mChannels,
                                    &outputDesc->mLatency,
                                    outputDesc->mFlags);

    if (mHardwareOutput == 0) {
        LOGE("Failed to initialize hardware output stream, samplingRate: %d, format %d, channels %d",
                outputDesc->mSamplingRate, outputDesc->mFormat, outputDesc->mChannels);
    } else {
        mOutputs.add(mHardwareOutput, outputDesc);
        setOutputDevice(mHardwareOutput, (uint32_t)AudioSystem::DEVICE_OUT_SPEAKER, true);
    }

    mA2dpOutput = 0;
    mDuplicatedOutput = 0;
}

AudioPolicyManager::~AudioPolicyManager()
{
   for (size_t i = 0; i < mOutputs.size(); i++) {
        mpClientInterface->closeOutput(mOutputs.keyAt(i));
        delete mOutputs.valueAt(i);
   }
   mOutputs.clear();
   for (size_t i = 0; i < mInputs.size(); i++) {
        mpClientInterface->closeInput(mInputs.keyAt(i));
        delete mInputs.valueAt(i);
   }
   mInputs.clear();
}

// ---

audio_io_handle_t AudioPolicyManager::getOutputForDevice(uint32_t device)
{
    audio_io_handle_t output = 0;
    uint32_t lDevice;

    for (size_t i = 0; i < mOutputs.size(); i++) {
        lDevice = mOutputs.valueAt(i)->device();
        LOGV("getOutputForDevice() output %d devices %x", mOutputs.keyAt(i), lDevice);

        // We are only considering outputs connected to a mixer here => exclude direct outputs
        if ((lDevice == device) &&
           !(mOutputs.valueAt(i)->mFlags & AudioSystem::OUTPUT_FLAG_DIRECT)) {
            output = mOutputs.keyAt(i);
            LOGV("getOutputForDevice() found output %d for device %x", output, device);
            break;
        }
    }
    return output;
}

AudioPolicyManager::routing_strategy AudioPolicyManager::getStrategy(AudioSystem::stream_type stream)
{
    // stream to strategy mapping
    switch (stream) {
    case AudioSystem::VOICE_CALL:
    case AudioSystem::BLUETOOTH_SCO:
        return STRATEGY_PHONE;
    case AudioSystem::RING:
    case AudioSystem::NOTIFICATION:
    case AudioSystem::ALARM:
    case AudioSystem::ENFORCED_AUDIBLE:
        return STRATEGY_SONIFICATION;
    case AudioSystem::DTMF:
        return STRATEGY_DTMF;
    default:
        LOGE("unknown stream type");
    case AudioSystem::SYSTEM:
        // NOTE: SYSTEM stream uses MEDIA strategy because muting music and switching outputs
        // while key clicks are played produces a poor result
    case AudioSystem::TTS:
    case AudioSystem::MUSIC:
        return STRATEGY_MEDIA;
    }
}

uint32_t AudioPolicyManager::getDeviceForStrategy(routing_strategy strategy)
{
    uint32_t device = 0;

    switch (strategy) {
    case STRATEGY_DTMF:
        if (mPhoneState != AudioSystem::MODE_IN_CALL) {
            // when off call, DTMF strategy follows the same rules as MEDIA strategy
            device = getDeviceForStrategy(STRATEGY_MEDIA);
            break;
        }
        // when in call, DTMF and PHONE strategies follow the same rules
        // FALL THROUGH

    case STRATEGY_PHONE:
        // for phone strategy, we first consider the forced use and then the available devices by order
        // of priority
        switch (mForceUse[AudioSystem::FOR_COMMUNICATION]) {
        case AudioSystem::FORCE_BT_SCO:
            if (mPhoneState != AudioSystem::MODE_IN_CALL || strategy != STRATEGY_DTMF) {
                device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_CARKIT;
                if (device) break;
            }
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_HEADSET;
            if (device) break;
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_SCO;
            if (device) break;
            // if SCO device is requested but no SCO device is available, fall back to default case
            // FALL THROUGH

        default:    // FORCE_NONE
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADPHONE;
            if (device) break;
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADSET;
            if (device) break;
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_EARPIECE;
            if (device == 0) {
                LOGE("getDeviceForStrategy() earpiece device not found");
            }
            break;

        case AudioSystem::FORCE_SPEAKER:
            if (mPhoneState != AudioSystem::MODE_IN_CALL || strategy != STRATEGY_DTMF) {
                device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_CARKIT;
                if (device) break;
            }
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_SPEAKER;
            if (device == 0) {
                LOGE("getDeviceForStrategy() speaker device not found");
            }
            break;
        }
    break;

    case STRATEGY_SONIFICATION:

        // If incall, just select the STRATEGY_PHONE device: The rest of the behavior is handled by
        // handleIncallSonification().
        if (mPhoneState == AudioSystem::MODE_IN_CALL) {
            device = getDeviceForStrategy(STRATEGY_PHONE);
            break;
        }
        device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_SPEAKER;
        if (device == 0) {
            LOGE("getDeviceForStrategy() speaker device not found");
        }
        // The second device used for sonification is the same as the device used by media strategy
        // FALL THROUGH

    case STRATEGY_MEDIA: {
        uint32_t device2 = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_AUX_DIGITAL;
        if (device2 == 0) {
            device2 = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP;
            if (device2 == 0) {
                device2 = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES;
                if (device2 == 0) {
                    device2 = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER;
                    if (device2 == 0) {
                        device2 = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADPHONE;
                        if (device2 == 0) {
                            device2 = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADSET;
                            if (device2 == 0) {
                                device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_SPEAKER;
                                if (device == 0) {
                                    LOGE("getDeviceForStrategy() speaker device not found");
                                }
                            }
                        }
                    }
                }
            }
        }
        // device is DEVICE_OUT_SPEAKER if we come from case STRATEGY_SONIFICATION, 0 otherwise
        device |= device2;
        // Do not play media stream if in call and the requested device would change the hardware
        // output routing
        if (mPhoneState == AudioSystem::MODE_IN_CALL &&
            !AudioSystem::isA2dpDevice((AudioSystem::audio_devices)device) &&
            device != getDeviceForStrategy(STRATEGY_PHONE)) {
            device = 0;
            LOGV("getDeviceForStrategy() incompatible media and phone devices");
        }
        } break;

    default:
        LOGW("getDeviceForStrategy() unknown strategy: %d", strategy);
        break;
    }

    LOGV("getDeviceForStrategy() strategy %d, device %x", strategy, device);
    return device;
}

void AudioPolicyManager::setOutputDevice(audio_io_handle_t output, uint32_t device, bool force, int delayMs)
{
    LOGV("setOutputDevice() output %d device %x delayMs %d", output, device, delayMs);
    if (mOutputs.indexOfKey(output) < 0) {
        LOGW("setOutputDevice() unknown output %d", output);
        return;
    }
#ifdef WITH_A2DP
    if (output == mHardwareOutput) {
        // clear A2DP devices from device bit field here so that the caller does not have to
        // do it in case of multiple device selections
        uint32_t device2 = device & ~AudioSystem::DEVICE_OUT_SPEAKER;
        if (AudioSystem::isA2dpDevice((AudioSystem::audio_devices)device2)) {
            LOGV("setOutputDevice() removing A2DP device");
            device &= ~device2;
        }
    } else if (output == mA2dpOutput) {
        // clear hardware devices from device bit field here so that the caller does not have to
        // do it in case of multiple device selections (the second device is always DEVICE_OUT_SPEAKER)
        // in this case
        device &= ~AudioSystem::DEVICE_OUT_SPEAKER;
    }
#endif

    // doing this check here allows the caller to call setOutputDevice() without conditions
    if (device == 0) return;

    uint32_t oldDevice = (uint32_t)mOutputs.valueFor(output)->device();
    // Do not change the routing if the requested device is the same as current device. Doing this check
    // here allows the caller to call setOutputDevice() without conditions
    if (device == oldDevice && !force) {
        LOGV("setOutputDevice() setting same device %x for output %d", device, output);
        return;
    }

    mOutputs.valueFor(output)->mDevice = device;
    // mute media streams if both speaker and headset are selected
    if (device == (AudioSystem::DEVICE_OUT_SPEAKER | AudioSystem::DEVICE_OUT_WIRED_HEADSET) ||
        device == (AudioSystem::DEVICE_OUT_SPEAKER | AudioSystem::DEVICE_OUT_WIRED_HEADPHONE)) {
        setStrategyMute(STRATEGY_MEDIA, true, output);
        // wait for the PCM output buffers to empty before proceeding with the rest of the command
        usleep(mOutputs.valueFor(output)->mLatency*2*1000);
    }
    // suspend A2D output if SCO device is selected
    if (AudioSystem::isBluetoothScoDevice((AudioSystem::audio_devices)device)) {
         if (mA2dpOutput && mScoDeviceAddress == mA2dpDeviceAddress) {
             mpClientInterface->suspendOutput(mA2dpOutput);
         }
    }
    // do the routing
    AudioParameter param = AudioParameter();
    param.addInt(String8(AudioParameter::keyRouting), (int)device);
    mpClientInterface->setParameters(mHardwareOutput, param.toString(), delayMs);
    // update stream volumes according to new device
    applyStreamVolumes(output, device, delayMs);

    // if disconnecting SCO device, restore A2DP output
    if (AudioSystem::isBluetoothScoDevice((AudioSystem::audio_devices)oldDevice)) {
         if (mA2dpOutput && mScoDeviceAddress == mA2dpDeviceAddress) {
             LOGV("restore A2DP output");
             mpClientInterface->restoreOutput(mA2dpOutput);
         }
    }
    // if changing from a combined headset + speaker route, unmute media streams
    if (oldDevice == (AudioSystem::DEVICE_OUT_SPEAKER | AudioSystem::DEVICE_OUT_WIRED_HEADSET) ||
        oldDevice == (AudioSystem::DEVICE_OUT_SPEAKER | AudioSystem::DEVICE_OUT_WIRED_HEADPHONE)) {
        setStrategyMute(STRATEGY_MEDIA, false, output, delayMs);
    }
}

uint32_t AudioPolicyManager::getDeviceForInputSource(int inputSource)
{
    uint32_t device;

    switch(inputSource) {
    case AUDIO_SOURCE_DEFAULT:
    case AUDIO_SOURCE_MIC:
    case AUDIO_SOURCE_VOICE_RECOGNITION:
        if (mForceUse[AudioSystem::FOR_RECORD] == AudioSystem::FORCE_BT_SCO &&
            mAvailableInputDevices & AudioSystem::DEVICE_IN_BLUETOOTH_SCO_HEADSET) {
            device = AudioSystem::DEVICE_IN_BLUETOOTH_SCO_HEADSET;
        } else if (mAvailableInputDevices & AudioSystem::DEVICE_IN_WIRED_HEADSET) {
            device = AudioSystem::DEVICE_IN_WIRED_HEADSET;
        } else {
            device = AudioSystem::DEVICE_IN_BUILTIN_MIC;
        }
        break;
    case AUDIO_SOURCE_CAMCORDER:
        device = AudioSystem::DEVICE_IN_BUILTIN_MIC;
        break;
    case AUDIO_SOURCE_VOICE_UPLINK:
    case AUDIO_SOURCE_VOICE_DOWNLINK:
    case AUDIO_SOURCE_VOICE_CALL:
        device = AudioSystem::DEVICE_IN_VOICE_CALL;
        break;
    default:
        LOGW("getInput() invalid input source %d", inputSource);
        device = 0;
        break;
    }
    return device;
}

audio_io_handle_t AudioPolicyManager::getActiveInput()
{
    for (size_t i = 0; i < mInputs.size(); i++) {
        if (mInputs.valueAt(i)->mRefCount > 0) {
            return mInputs.keyAt(i);
        }
    }
    return 0;
}

float AudioPolicyManager::computeVolume(int stream, int index, audio_io_handle_t output, uint32_t device)
{
    float volume = 1.0;
    AudioOutputDescriptor *outputDesc = mOutputs.valueFor(output);
    StreamDescriptor &streamDesc = mStreams[stream];

    if (device == 0) {
        device = outputDesc->device();
    }

    int volInt = (100 * (index - streamDesc.mIndexMin)) / (streamDesc.mIndexMax - streamDesc.mIndexMin);
    volume = AudioSystem::linearToLog(volInt);

    // if a heaset is connected, apply the following rules to ring tones and notifications
    // to avoid sound level bursts in user's ears:
    // - always attenuate ring tones and notifications volume by 6dB
    // - if music is playing, always limit the volume to current music volume,
    // with a minimum threshold at -36dB so that notification is always perceived.
    if ((device &
        (AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP |
        AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES |
        AudioSystem::DEVICE_OUT_WIRED_HEADSET |
        AudioSystem::DEVICE_OUT_WIRED_HEADPHONE)) &&
        (getStrategy((AudioSystem::stream_type)stream) == STRATEGY_SONIFICATION)) {
        volume *= SONIFICATION_HEADSET_VOLUME_FACTOR;
        // when the phone is ringing we must consider that music could have been paused just before
        // by the music application and behave as if music was active if the last music track was
        // just stopped
        if (outputDesc->isUsedByStream(AudioSystem::MUSIC) || mLimitRingtoneVolume) {
            float musicVol = computeVolume(AudioSystem::MUSIC, mStreams[AudioSystem::MUSIC].mIndexCur, output, device);
            float minVol = (musicVol > SONIFICATION_HEADSET_VOLUME_MIN) ? musicVol : SONIFICATION_HEADSET_VOLUME_MIN;
            if (volume > minVol) {
                volume = minVol;
                LOGV("computeVolume limiting volume to %f musicVol %f", minVol, musicVol);
            }
        }
    }

    return volume;
}

status_t AudioPolicyManager::checkAndSetVolume(int stream, int index, audio_io_handle_t output, uint32_t device, int delayMs, bool force)
{

    // do not change actual stream volume if the stream is muted
    if (mStreams[stream].mMuteCount != 0) {
        LOGV("checkAndSetVolume() stream %d muted count %d", stream, mStreams[stream].mMuteCount);
        return NO_ERROR;
    }

    // do not change in call volume if bluetooth is connected and vice versa
    if ((stream == AudioSystem::VOICE_CALL && mForceUse[AudioSystem::FOR_COMMUNICATION] == AudioSystem::FORCE_BT_SCO) ||
        (stream == AudioSystem::BLUETOOTH_SCO && mForceUse[AudioSystem::FOR_COMMUNICATION] != AudioSystem::FORCE_BT_SCO)) {
        LOGV("checkAndSetVolume() cannot set stream %d volume with force use = %d for comm",
             stream, mForceUse[AudioSystem::FOR_COMMUNICATION]);
        return INVALID_OPERATION;
    }

    float volume = computeVolume(stream, index, output, device);
    // do not set volume if the float value did not change
    if (volume != mOutputs.valueFor(output)->mCurVolume[stream] || force) {
        mOutputs.valueFor(output)->mCurVolume[stream] = volume;
        LOGV("setStreamVolume() for output %d stream %d, volume %f, delay %d", output, stream, volume, delayMs);
        if (stream == AudioSystem::VOICE_CALL ||
            stream == AudioSystem::DTMF ||
            stream == AudioSystem::BLUETOOTH_SCO) {
            float voiceVolume = -1.0;
            // offset value to reflect actual hardware volume that never reaches 0
            // 1% corresponds roughly to first step in VOICE_CALL stream volume setting (see AudioService.java)
            volume = 0.01 + 0.99 * volume;
            if (stream == AudioSystem::VOICE_CALL) {
                voiceVolume = (float)index/(float)mStreams[stream].mIndexMax;
            } else if (stream == AudioSystem::BLUETOOTH_SCO) {
                voiceVolume = 1.0;
            }
            if (voiceVolume >= 0 && output == mHardwareOutput) {
                mpClientInterface->setVoiceVolume(voiceVolume, delayMs);
            }
        }
        mpClientInterface->setStreamVolume((AudioSystem::stream_type)stream, volume, output, delayMs);
    }

    return NO_ERROR;
}

void AudioPolicyManager::applyStreamVolumes(audio_io_handle_t output, uint32_t device, int delayMs)
{
    LOGV("applyStreamVolumes() for output %d and device %x", output, device);

    for (int stream = 0; stream < AudioSystem::NUM_STREAM_TYPES; stream++) {
        checkAndSetVolume(stream, mStreams[stream].mIndexCur, output, device, delayMs);
    }
}

void AudioPolicyManager::setStrategyMute(routing_strategy strategy, bool on, audio_io_handle_t output, int delayMs)
{
    LOGV("setStrategyMute() strategy %d, mute %d, output %d", strategy, on, output);
    for (int stream = 0; stream < AudioSystem::NUM_STREAM_TYPES; stream++) {
        if (getStrategy((AudioSystem::stream_type)stream) == strategy) {
            setStreamMute(stream, on, output, delayMs);
        }
    }
}

void AudioPolicyManager::setStreamMute(int stream, bool on, audio_io_handle_t output, int delayMs)
{
    StreamDescriptor &streamDesc = mStreams[stream];
    uint32_t device = mOutputs.valueFor(output)->mDevice;

    LOGV("setStreamMute() stream %d, mute %d, output %d, mMuteCount %d", stream, on, output, streamDesc.mMuteCount);

    if (on) {
        if (streamDesc.mMuteCount == 0) {
            if (streamDesc.mCanBeMuted) {
                checkAndSetVolume(stream, 0, output, device, delayMs);
            }
        }
        // increment mMuteCount after calling checkAndSetVolume() so that volume change is not ignored
        streamDesc.mMuteCount++;
    } else {
        if (streamDesc.mMuteCount == 0) {
            LOGW("setStreamMute() unmuting non muted stream!");
            return;
        }
        if (--streamDesc.mMuteCount == 0) {
            checkAndSetVolume(stream, streamDesc.mIndexCur, output, device, delayMs);
        }
    }
}

void AudioPolicyManager::handleIncallSonification(int stream, bool starting, bool stateChange)
{
    // if the stream pertains to sonification strategy and we are in call we must
    // mute the stream if it is low visibility. If it is high visibility, we must play a tone
    // in the device used for phone strategy and play the tone if the selected device does not
    // interfere with the device used for phone strategy
    // if stateChange is true, we are called from setPhoneState() and we must mute or unmute as
    // many times as there are active tracks on the output

    if (getStrategy((AudioSystem::stream_type)stream) == STRATEGY_SONIFICATION) {
        AudioOutputDescriptor *outputDesc = mOutputs.valueFor(mHardwareOutput);
        LOGV("handleIncallSonification() stream %d starting %d device %x stateChange %d",
                stream, starting, outputDesc->mDevice, stateChange);
        if (outputDesc->isUsedByStream((AudioSystem::stream_type)stream)) {
            int muteCount = 1;
            if (stateChange) {
                muteCount = outputDesc->mRefCount[stream];
            }
            if (AudioSystem::isLowVisibility((AudioSystem::stream_type)stream)) {
                LOGV("handleIncallSonification() low visibility, muteCount %d", muteCount);
                for (int i = 0; i < muteCount; i++) {
                    setStreamMute(stream, starting, mHardwareOutput);
                }
            } else {
                LOGV("handleIncallSonification() high visibility ");
                if (outputDesc->mDevice & getDeviceForStrategy(STRATEGY_PHONE)) {
                    LOGV("handleIncallSonification() high visibility muted, muteCount %d", muteCount);
                    for (int i = 0; i < muteCount; i++) {
                        setStreamMute(stream, starting, mHardwareOutput);
                    }
                }
                if (starting) {
                    mpClientInterface->startTone(ToneGenerator::TONE_SUP_CALL_WAITING, AudioSystem::VOICE_CALL);
                } else {
                    mpClientInterface->stopTone();
                }
            }
        }
    }
}

uint32_t AudioPolicyManager::getMaxEffectsCpuLoad()
{
//TODO ..newly added function in Gingerbread
   LOGD("Inside.%s. Need implementation\n",__func__);
   return 0;
}

uint32_t AudioPolicyManager::getMaxEffectsMemory()
{
//TODO ..newly added function in Gingerbread
   LOGD("Inside.%s. Need implementation\n",__func__);
   return 0;
}


uint32_t AudioPolicyManager::getStrategyForStream(AudioSystem::stream_type stream) 
{
//TODO ..newly added function in Gingerbread
   LOGD("Inside.%s. Need implementation\n",__func__);
   return 0;

}

audio_io_handle_t AudioPolicyManager::getOutputForEffect(effect_descriptor_t *desc) 
{

//TODO ..newly added function in Gingerbread
   LOGD("Inside.%s. Need implementation\n",__func__);
   return 0;

}

status_t AudioPolicyManager::registerEffect(effect_descriptor_t *desc,
                                    audio_io_handle_t output,
                                    uint32_t strategy,
                                    int session,
                                    int id) 
{

//TODO ..newly added function in Gingerbread
   LOGD("Inside.%s. Need implementation\n",__func__);
   return 0;
}

status_t AudioPolicyManager::unregisterEffect(int id) 
{
//TODO ..newly added function in Gingerbread
   LOGD("Inside.%s. Need implementation\n",__func__);
   return 0;
}
// --- AudioOutputDescriptor class implementation

AudioPolicyManager::AudioOutputDescriptor::AudioOutputDescriptor()
    : mSamplingRate(0), mFormat(0), mChannels(0), mLatency(0),
    mFlags((AudioSystem::output_flags)0), mDevice(0), mOutput1(0), mOutput2(0)
{
    // clear usage count for all stream types
    for (int i = 0; i < AudioSystem::NUM_STREAM_TYPES; i++) {
        mRefCount[i] = 0;
        mCurVolume[i] = -1.0;
    }
}

uint32_t AudioPolicyManager::AudioOutputDescriptor::device()
{
    uint32_t device = 0;
    if (isDuplicated()) {
        device = mOutput1->mDevice | mOutput2->mDevice;
    } else {
        device = mDevice;
    }
    return device;
}

void AudioPolicyManager::AudioOutputDescriptor::changeRefCount(AudioSystem::stream_type stream, int delta)
{
    // forward usage count change to attached outputs
    if (isDuplicated()) {
        mOutput1->changeRefCount(stream, delta);
        mOutput2->changeRefCount(stream, delta);
    }
    if ((delta + (int)mRefCount[stream]) < 0) {
        LOGW("changeRefCount() invalid delta %d for stream %d, refCount %d", delta, stream, mRefCount[stream]);
        mRefCount[stream] = 0;
        return;
    }
    mRefCount[stream] += delta;
    LOGV("changeRefCount() stream %d, count %d", stream, mRefCount[stream]);
}

bool AudioPolicyManager::AudioOutputDescriptor::isUsedByStrategy(routing_strategy strategy)
{
    for (int i = 0; i < (int)AudioSystem::NUM_STREAM_TYPES; i++) {
        if (AudioPolicyManager::getStrategy((AudioSystem::stream_type)i) == strategy &&
            isUsedByStream((AudioSystem::stream_type)i)) {
            return true;
        }
    }
    return false;
}

status_t AudioPolicyManager::AudioOutputDescriptor::dump(int fd)
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;

    snprintf(buffer, SIZE, " Sampling rate: %d\n", mSamplingRate);
    result.append(buffer);
    snprintf(buffer, SIZE, " Format: %d\n", mFormat);
    result.append(buffer);
    snprintf(buffer, SIZE, " Channels: %08x\n", mChannels);
    result.append(buffer);
    snprintf(buffer, SIZE, " Latency: %d\n", mLatency);
    result.append(buffer);
    snprintf(buffer, SIZE, " Flags %08x\n", mFlags);
    result.append(buffer);
    snprintf(buffer, SIZE, " Devices %08x\n", mDevice);
    result.append(buffer);
    snprintf(buffer, SIZE, " Stream volume refCount\n");
    result.append(buffer);
    for (int i = 0; i < AudioSystem::NUM_STREAM_TYPES; i++) {
        snprintf(buffer, SIZE, " %02d     %.03f  %d\n", i, mCurVolume[i], mRefCount[i]);
        result.append(buffer);
    }
    write(fd, result.string(), result.size());

    return NO_ERROR;
}

// --- AudioInputDescriptor class implementation

AudioPolicyManager::AudioInputDescriptor::AudioInputDescriptor()
    : mSamplingRate(0), mFormat(0), mChannels(0),
     mAcoustics((AudioSystem::audio_in_acoustics)0), mDevice(0), mRefCount(0)
{
}

status_t AudioPolicyManager::AudioInputDescriptor::dump(int fd)
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;

    snprintf(buffer, SIZE, " Sampling rate: %d\n", mSamplingRate);
    result.append(buffer);
    snprintf(buffer, SIZE, " Format: %d\n", mFormat);
    result.append(buffer);
    snprintf(buffer, SIZE, " Channels: %08x\n", mChannels);
    result.append(buffer);
    snprintf(buffer, SIZE, " Acoustics %08x\n", mAcoustics);
    result.append(buffer);
    snprintf(buffer, SIZE, " Devices %08x\n", mDevice);
    result.append(buffer);
    snprintf(buffer, SIZE, " Ref Count %d\n", mRefCount);
    result.append(buffer);
    write(fd, result.string(), result.size());

    return NO_ERROR;
}

// --- StreamDescriptor class implementation

void AudioPolicyManager::StreamDescriptor::dump(char* buffer, size_t size)
{
    snprintf(buffer, size, "      %02d         %02d         %02d         %02d          %d\n",
            mIndexMin,
            mIndexMax,
            mIndexCur,
            mMuteCount,
            mCanBeMuted);
}


}; // namespace android
