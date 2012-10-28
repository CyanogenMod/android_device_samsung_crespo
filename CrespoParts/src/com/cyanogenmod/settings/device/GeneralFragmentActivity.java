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
import android.preference.ListPreference;
import android.preference.Preference;
import android.preference.Preference.OnPreferenceClickListener;
import android.preference.PreferenceActivity;
import android.preference.PreferenceFragment;
import android.preference.PreferenceManager;
import android.preference.PreferenceScreen;
import android.util.Log;
import android.view.View;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.content.DialogInterface.OnClickListener;
import android.widget.TextView;

import com.cyanogenmod.settings.device.R;

public class GeneralFragmentActivity extends PreferenceFragment implements OnPreferenceClickListener {

    private static final String CPU_DEEPIDLE_FILE = "/sys/class/misc/deepidle/enabled";
    private static final String CPU_DEEPIDLE_STATS = "/sys/class/misc/deepidle/idle_stats_list";
    private static final String CPU_DEEPIDLE_RESET = "/sys/class/misc/deepidle/reset_stats";
    private static final String TOUCHKEY_NOTIFICATION_FILE = "/sys/class/misc/notification/enabled";
    private static final String PREF_ENABLED = "1";
    private static final String TAG = "CrespoParts_General";
    private static final String BLD_FILE = "/sys/class/misc/backlightdimmer/enabled";

    private CheckBoxPreference mDeepIdle;
    private CheckBoxPreference mNotification;
    private PreferenceScreen mIdleStats;
    private bldTuningPreference mbldTuning;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        addPreferencesFromResource(R.xml.general_preferences);

        PreferenceScreen prefSet = getPreferenceScreen();
        mDeepIdle = (CheckBoxPreference) findPreference(DeviceSettings.KEY_DEEPIDLE);
        mNotification = (CheckBoxPreference) findPreference(DeviceSettings.KEY_NOTIFICATION);
        mIdleStats = (PreferenceScreen) findPreference(DeviceSettings.KEY_DEEPIDLE_STATS);

        if (isSupported(CPU_DEEPIDLE_FILE)) {
            mDeepIdle.setChecked(PREF_ENABLED.equals(Utils.readOneLine(CPU_DEEPIDLE_FILE)));
            mIdleStats.setOnPreferenceClickListener(this);
        } else {
            mDeepIdle.setEnabled(false);
            mIdleStats.setEnabled(false);
        }

        if (isSupported(TOUCHKEY_NOTIFICATION_FILE)) {
            mNotification.setChecked(PREF_ENABLED.equals(Utils.readOneLine(TOUCHKEY_NOTIFICATION_FILE)));
        } else {
            mNotification.setEnabled(false);
        }

        mbldTuning = (bldTuningPreference) findPreference(DeviceSettings.KEY_BLD_TUNING);
        if(mbldTuning != null)
            mbldTuning.setEnabled(bldTuningPreference.isSupported());

    }

    @Override
    public boolean onPreferenceTreeClick(PreferenceScreen preferenceScreen, Preference preference) {

        String boxValue;
        String key = preference.getKey();

        Log.w(TAG, "key: " + key);
        if (key.equals(DeviceSettings.KEY_DEEPIDLE)) {
            final CheckBoxPreference chkPref = (CheckBoxPreference) preference;
            boxValue = chkPref.isChecked() ? "1" : "0";
            Utils.writeValue(CPU_DEEPIDLE_FILE, boxValue);
        } else if (key.equals(DeviceSettings.KEY_NOTIFICATION)) {
            final CheckBoxPreference chkPref = (CheckBoxPreference) preference;
            boxValue = chkPref.isChecked() ? "1" : "0";
            Utils.writeValue(TOUCHKEY_NOTIFICATION_FILE, boxValue);
        }

        return true;
    }

    public static boolean isSupported(String FILE) {
        return Utils.fileExists(FILE);
    }

    public static void restore(Context context) {
        SharedPreferences sharedPrefs = PreferenceManager.getDefaultSharedPreferences(context);
        if (isSupported(CPU_DEEPIDLE_FILE)) {
            String sDefaultValue = Utils.readOneLine(CPU_DEEPIDLE_FILE);
            Utils.writeValue(CPU_DEEPIDLE_FILE, sharedPrefs.getBoolean(DeviceSettings.KEY_DEEPIDLE,
                             PREF_ENABLED.equals(sDefaultValue)));
        }
        if (isSupported(TOUCHKEY_NOTIFICATION_FILE)) {
            String sDefaultValue = Utils.readOneLine(TOUCHKEY_NOTIFICATION_FILE);
            Utils.writeValue(TOUCHKEY_NOTIFICATION_FILE, sharedPrefs.getBoolean(DeviceSettings.KEY_NOTIFICATION,
                             PREF_ENABLED.equals(sDefaultValue)));
        }
    }

    private void showIdleStatsDialog() {
        // display dialog
        final View content = getActivity().getLayoutInflater().inflate(R.layout.idle_stats_dialog, null);

        String sStatsLine = Utils.readOneLine(CPU_DEEPIDLE_STATS);
        String[] sValues = sStatsLine.split(" ");
        ((TextView)content.findViewById(R.id.time1)).setText(sValues[0]);
        ((TextView)content.findViewById(R.id.time2)).setText(sValues[2]);
        ((TextView)content.findViewById(R.id.time3)).setText(sValues[4]);
        ((TextView)content.findViewById(R.id.avg1)).setText(sValues[1]);
        ((TextView)content.findViewById(R.id.avg2)).setText(sValues[3]);
        ((TextView)content.findViewById(R.id.avg3)).setText(sValues[5]);

        AlertDialog.Builder builder = new AlertDialog.Builder(getActivity());
        builder.setTitle(getString(R.string.label_deepidle_stats));
        builder.setView(content);
        builder.setPositiveButton(getString(R.string.label_reset), new OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, int which) {
                Utils.writeValue(CPU_DEEPIDLE_RESET, "1");
                dialog.dismiss();
            }
        });
        builder.setNegativeButton(getString(R.string.label_close), new OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, int which) {
                dialog.dismiss();
            }
        });
        builder.show();
    }

    @Override
    public boolean onPreferenceClick(Preference preference) {
        boolean ret = false;
        if(preference.getKey().equals(DeviceSettings.KEY_DEEPIDLE_STATS)) {
            showIdleStatsDialog();
            ret = true;
        }
        return ret;
    }
}
