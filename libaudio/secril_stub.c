/*
 * Copyright (C) 2010 The Android Open Source Project
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

#include "secril-client.h"

int isConnected_RILD(HRilClient client) {
    return 0;
}

int Connect_RILD(HRilClient client) {
    return 0;
}

int InvokeOemRequestHookRaw(HRilClient client, char *data, size_t len) {
    return 0;
}

int RegisterRequestCompleteHandler(HRilClient client, uint32_t id, RilOnComplete handler) {
    return 0;
}

int RegisterUnsolicitedHandler(HRilClient client, uint32_t id, RilOnUnsolicited handler) {
    return 0;
}

int Disconnect_RILD(HRilClient client) {
    return 0;
}

int CloseClient_RILD(HRilClient client) {
    return 0;
}

HRilClient OpenClient_RILD(void) {
    return 0;
}
