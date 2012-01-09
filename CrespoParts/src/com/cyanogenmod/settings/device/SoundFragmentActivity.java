/*
 * Copyright (C) 2011 The CyanogenMod Project
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

package com.cyanogenmod.settings.device;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.preference.CheckBoxPreference;
import android.preference.Preference;
import android.preference.PreferenceActivity;
import android.preference.PreferenceFragment;
import android.preference.PreferenceManager;
import android.preference.PreferenceScreen;
import android.util.Log;

import com.cyanogenmod.settings.device.R;

public class SoundFragmentActivity extends PreferenceFragment {

    public static final String aOptionControl[][] = {
        {"/sys/class/misc/voodoo_sound_control/enable","pref_wm8994_control_enable"},
        {"/sys/class/misc/voodoo_sound/speaker_tuning","pref_wm8994_speaker_tuning"},
        {"/sys/class/misc/voodoo_sound/mono_downmix","pref_wm8994_mono_downmix"},
        {"/sys/class/misc/voodoo_sound/stereo_expansion","pref_wm8994_stereo_expansion"},
        {"/sys/class/misc/voodoo_sound/dac_direct","pref_wm8994_dac_direct"},
        {"/sys/class/misc/voodoo_sound/dac_osr128","pref_wm8994_dac_osr128"},
        {"/sys/class/misc/voodoo_sound/adc_osr128","pref_wm8994_adc_osr128"},
        {"/sys/class/misc/voodoo_sound/fll_tuning","pref_wm8994_fll_tuning"}
    };
    private static final Integer iTotalOptions = aOptionControl.length;
    private CheckBoxPreference cbpStatus[] = new CheckBoxPreference[iTotalOptions];

    // Misc
    private static final String PREF_ENABLED = "1";
    private static final String TAG = "CrespoParts_WM8994ControlSound";

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        addPreferencesFromResource(R.xml.sound_preferences);

        PreferenceScreen prefSet = getPreferenceScreen();

        Integer iPosition;
        for(iPosition=0;iPosition<iTotalOptions;iPosition++) {
            if (isSupported(aOptionControl[iPosition][0])) {
                cbpStatus[iPosition] = (CheckBoxPreference) prefSet.findPreference(aOptionControl[iPosition][1]);
                cbpStatus[iPosition].setChecked(PREF_ENABLED.equals(Utils.readOneLine(aOptionControl[iPosition][0])));
            }
        }
    }

    @Override
    public boolean onPreferenceTreeClick(PreferenceScreen preferenceScreen, Preference preference) {

        Integer iPosition;
        String boxValue;
        for(iPosition=0;iPosition<iTotalOptions;iPosition++) {
            if (preference == cbpStatus[iPosition]) {
                Log.d(TAG,"Procesando Salida: " + aOptionControl[iPosition][1] + " .. " + aOptionControl[iPosition][0]);
                boxValue = cbpStatus[iPosition].isChecked() ? "1" : "0";
                Utils.writeValue(aOptionControl[iPosition][0], boxValue);
            }
        }

        return true;
    }

    public static boolean isSupported(String FILE) {
        return Utils.fileExists(FILE);
    }

    public static void restore(Context context) {

        Integer iPosition;
        for(iPosition=0;iPosition<iTotalOptions;iPosition++) {
            if (isSupported(aOptionControl[iPosition][0])) {
                SharedPreferences sharedPrefs = PreferenceManager.getDefaultSharedPreferences(context);
                Utils.writeValue(aOptionControl[iPosition][0], sharedPrefs.getBoolean(aOptionControl[iPosition][1], PREF_ENABLED.equals(Utils.readOneLine(aOptionControl[iPosition][0]))));
            }
        }
    }
}
