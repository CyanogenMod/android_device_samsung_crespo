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
import java.io.File;

public class DisplayColorCalibration {
    private static final String[] FILE_PATH = new String[] {
            "/sys/class/misc/samoled_color/red_multiplier",
            "/sys/class/misc/samoled_color/green_multiplier",
            "/sys/class/misc/samoled_color/blue_multiplier"
    };

    public static int getMaxValue() {
        return 255;
    }

    public static int getMinValue() {
        return 0;
    }

    public static int getDefValue() {
        return 127;
    }

    public static boolean fileExists(String filename) {
        return new File(filename).exists();
    }

    /**
     * Check whether the running kernel supports color tuning or not.
     *
     * @return Whether color tuning is supported or not
     */
    public static boolean isSupported() {
        boolean supported = true;
        for (String filePath : FILE_PATH) {
            if (!fileExists(filePath)) {
                supported = false;
                break;
            }
        }

        return supported;
    }

    public static String getCurColors() {
        String value = "";
        for (int i = 0; i < 3; ++i) {
            long colorValue = Long.parseLong(FileUtils.readOneLine(FILE_PATH[i]), 10);
            colorValue >>= 24;

            value += Long.toString(colorValue);
            if (i != 2) {
                value += " ";
            }
        }

        return value;
    }

    public static boolean setColors(String colors) {
        String[] parts = colors.split(" ");

        for (int i = 0; i < 3; ++i) {
            long colorValue = Long.parseLong(parts[i], 10);
            colorValue <<= 24;
            --colorValue;
            if (colorValue < 0) {
                colorValue = 0;
            }
            if (!FileUtils.writeLine(FILE_PATH[i], Long.toString(colorValue))) {
                return false;
            }
        }

        return true;
    }
}
