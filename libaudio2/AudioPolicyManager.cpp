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
// AudioPolicyManager for crespo platform
// Common audio policy manager code is implemented in AudioPolicyManagerBase class
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


status_t AudioPolicyManager::startInput(audio_io_handle_t input)
{
    status_t status = AudioPolicyManagerBase::startInput(input);

    if (status == NO_ERROR) {
        AudioInputDescriptor *inputDesc = mInputs.valueFor(input);
        String8 key = String8("Input Source");
        String8 value;
        switch(inputDesc->mInputSource) {
        case AUDIO_SOURCE_VOICE_RECOGNITION:
            value = String8("Voice Recognition");
            break;
        case AUDIO_SOURCE_CAMCORDER:
            value = String8("Camcorder");
            break;
        case AUDIO_SOURCE_DEFAULT:
        case AUDIO_SOURCE_MIC:
            value = String8("Default");
        default:
            break;
        }
        AudioParameter param = AudioParameter();
        param.add(key, value);
        mpClientInterface->setParameters(input, param.toString());
    }
    return status;
}

}; // namespace android
