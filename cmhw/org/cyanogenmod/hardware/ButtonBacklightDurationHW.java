/*
 * Copyright (C) 2013 The CyanogenMod Project
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

package org.cyanogenmod.hardware;

import org.cyanogenmod.hardware.util.FileUtils;

public class ButtonBacklightDurationHW {

    private static final String FILE_PATH = "/sys/class/misc/backlightdimmer/delay";
    private static final String ENABLED_FILE_PATH = "/sys/class/misc/backlightdimmer/enabled";

    public static boolean isSupported() {
        return true;
    }

    public static boolean setEnabled(boolean enabled) {
        String value = "0";
        if (enabled) {
            value = "1";
        }
        return FileUtils.writeLine(ENABLED_FILE_PATH, value);
    }

    public static boolean getEnabled() {
        if (FileUtils.readOneLine(ENABLED_FILE_PATH) == "1") {
            return true;
        }
        return false;
    }

    public static boolean setDuration(int timeout) {
        return FileUtils.writeLine(FILE_PATH, String.valueOf(timeout * 1000));
    }

    public static int getMaxDuration() {
        return 30;
    }

    public static int getCurDuration() {
        return Integer.parseInt(FileUtils.readOneLine(FILE_PATH)) / 1000;
    }
}
