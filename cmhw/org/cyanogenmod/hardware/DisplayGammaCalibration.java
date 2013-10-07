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

public class DisplayGammaCalibration {

    private static final String[] FILE_PATH = new String[] {
        "/sys/class/misc/samoled_color/red_v1_offset",
        "/sys/class/misc/samoled_color/green_v1_offset",
        "/sys/class/misc/samoled_color/blue_v1_offset"
    };

    public static boolean isSupported() {
        for (String file : FILE_PATH) {
            if (!(new File(file).exists())) {
                return false;
            }
        }
        return true;
    }

    public static int getNumberOfControls() {
        return 1;
    }

    public static boolean setGamma(int controlIdx, String gamma) {
        String[] parts = gamma.split(" ");
        for (int i = 0; i < 3; ++i) {
            int gammaValue = Integer.parseInt(parts[i]) - 100;
            if (!FileUtils.writeLine(FILE_PATH[i], Integer.toString(gammaValue))) {
                return false;
            }
        }
        return true;
    }

    public static int getMaxValue(int controlIdx) {
        return 200;
    }

    public static int getMinValue(int controlIdx) {
        return 0;
    }

    public static String getCurGamma(int controlIdx) {
        String value = "";
        for (int i = 0; i < 3; ++i) {
            int gammaValue = Integer.parseInt(FileUtils.readOneLine(FILE_PATH[i]), 10);
            value += Integer.toString(gammaValue + 100);
            if (i != 2) {
                value += " ";
            }
        }
        return value;
    }
}
