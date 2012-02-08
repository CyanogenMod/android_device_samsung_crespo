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
import android.content.SharedPreferences.Editor;
import android.preference.DialogPreference;
import android.preference.PreferenceManager;
import android.util.AttributeSet;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.Button;
import android.widget.SeekBar;
import android.widget.TextView;
import android.util.Log;

/**
 * Special preference type that allows configuration of both the ring volume and
 * notification volume.
 */
public class LulzactiveTuningPreference extends DialogPreference implements OnClickListener {

    private static final String TAG = "LULZACTIVE...";


    private static final int[] SEEKBAR_ID = new int[] {
            R.id.inccpuload_seekbar, R.id.pumpup_seekbar, R.id.pumpdown_seekbar,
            R.id.upsampletime_seekbar, R.id.downsampletime_seekbar
    };

    private static final int[] VALUE_DISPLAY_ID = new int[] {
            R.id.inccpuload_value, R.id.pumpup_value, R.id.pumpdown_value,
            R.id.upsampletime_value, R.id.downsampletime_value
    };

    private static final String[] FILE_PATH = new String[] {
            "/sys/devices/system/cpu/cpufreq/lulzactive/inc_cpu_load",
            "/sys/devices/system/cpu/cpufreq/lulzactive/pump_up_step",
            "/sys/devices/system/cpu/cpufreq/lulzactive/pump_down_step",
            "/sys/devices/system/cpu/cpufreq/lulzactive/up_sample_time",
            "/sys/devices/system/cpu/cpufreq/lulzactive/down_sample_time"
    };

    private LulzactiveSeekBar mSeekBars[] = new LulzactiveSeekBar[5];

    private static final int [] MAX_VALUE = new int[] {
            99,
            5,
            5,
            50000,
            50000
    };

    private static final int [] MIN_VALUE = new int[] {
            30,
            1,
            1,
            10000,
            10000
    };


    // Track instances to know when to restore original color
    // (when the orientation changes, a new dialog is created before the old one
    // is destroyed)
    private static int sInstances = 0;

    public LulzactiveTuningPreference(Context context, AttributeSet attrs) {
        super(context, attrs);

        setDialogLayoutResource(R.layout.preference_dialog_lulzactive_tuning);
    }

    @Override
    protected void onBindDialogView(View view) {
        super.onBindDialogView(view);

        sInstances++;

        for (int i = 0; i < SEEKBAR_ID.length; i++) {
            SeekBar seekBar = (SeekBar) view.findViewById(SEEKBAR_ID[i]);
            TextView valueDisplay = (TextView) view.findViewById(VALUE_DISPLAY_ID[i]);
            mSeekBars[i] = new LulzactiveSeekBar(seekBar, valueDisplay, FILE_PATH[i], MAX_VALUE[i], MIN_VALUE[i]);
        }
        SetupButtonClickListeners(view);
    }

    private void SetupButtonClickListeners(View view) {
            Button mDefaultButton = (Button)view.findViewById(R.id.btnLulzDefault);
            Button mBatteryButton = (Button)view.findViewById(R.id.btnLulzBatt);
            Button mSpeedButton = (Button)view.findViewById(R.id.btnLulzSpeed);
            mDefaultButton.setOnClickListener(this);
            mBatteryButton.setOnClickListener(this);
            mSpeedButton.setOnClickListener(this);
        }

        @Override
    protected void onDialogClosed(boolean positiveResult) {
        super.onDialogClosed(positiveResult);
        int iBar;

        sInstances--;
        iBar = 0;

        if (positiveResult) {
            for (LulzactiveSeekBar csb : mSeekBars) {
                csb.save(iBar);
                iBar++;
            }
        } else if (sInstances == 0) {
            for (LulzactiveSeekBar csb : mSeekBars) {
                csb.reset(iBar);
                iBar++;
            }
        }
    }

    /**
     * Restore screen color tuning from SharedPreferences. (Write to kernel.)
     *
     * @param context The context to read the SharedPreferences from
     */
    public static void restore(Context context) {
        if (!isSupported()) {
            return;
        }

        SharedPreferences sharedPrefs = PreferenceManager.getDefaultSharedPreferences(context);
        for (String filePath : FILE_PATH) {
            String sDefaultValue = Utils.readOneLine(filePath);
            int iValue = sharedPrefs.getInt(filePath, Integer.valueOf(sDefaultValue));
            Utils.writeValue(filePath, String.valueOf((long) iValue));
        }
    }

    /**
     * Check whether the running kernel supports lulzactive tuning or not.
     *
     * @return Whether lulzactive tuning is supported or not
     */
    public static boolean isSupported() {
        boolean supported = true;
        for (String filePath : FILE_PATH) {
            if (!Utils.fileExists(filePath)) {
                supported = false;
            }
        }

        return supported;
    }

    class LulzactiveSeekBar implements SeekBar.OnSeekBarChangeListener {

        private String mFilePath;

        private int mOriginal;

        private SeekBar mSeekBar;

        private TextView mValueDisplay;

        public LulzactiveSeekBar(SeekBar seekBar, TextView valueDisplay, String filePath, int iMaxValue, int iMinValue) {
            int iValue;

            mSeekBar = seekBar;
            mValueDisplay = valueDisplay;
            mFilePath = filePath;

            SharedPreferences sharedPreferences = getSharedPreferences();

            // Read original value
            if (Utils.fileExists(mFilePath)) {
                String sDefaultValue = Utils.readOneLine(mFilePath);
                iValue = Integer.valueOf(sDefaultValue)-iMinValue;
                Log.w(TAG, "LulzactiveSeekBar: iValue: " + iValue + " File: " + mFilePath);
            } else {
                iValue = iMaxValue;
            }
            mOriginal = iValue;

            mSeekBar.setMax(iMaxValue-iMinValue);
            reset(CheckBarNumber(seekBar.getId()));
            mSeekBar.setOnSeekBarChangeListener(this);
        }

        public void reset(int iBar) {
            int iValue;

            iValue = mOriginal;
            mSeekBar.setProgress(iValue);
            updateValue(mOriginal, MIN_VALUE[iBar]);
        }

        public void save(int iBar) {
            int iValue;

            iValue = mSeekBar.getProgress() + MIN_VALUE[iBar];
            Editor editor = getEditor();
            editor.putInt(mFilePath, iValue);
            editor.commit();
        }

        public int CheckBarNumber(int iId) {
            int iBar;

            switch(iId) {
                case R.id.inccpuload_seekbar:
                    iBar = 0;
                    break;
                case R.id.pumpup_seekbar:
                    iBar = 1;
                    break;
                case R.id.pumpdown_seekbar:
                    iBar = 2;
                    break;
                case R.id.upsampletime_seekbar:
                    iBar = 3;
                    break;
                case R.id.downsampletime_seekbar:
                    iBar = 4;
                    break;
                default:
                    iBar = -1;
            }
            return iBar;
        }

        @Override
        public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
            int iValue, iSave;

            iValue = progress;
            iSave = iValue + MIN_VALUE[CheckBarNumber(seekBar.getId())];
            Utils.writeValue(mFilePath, String.valueOf((long) iSave));
            updateValue(iValue, MIN_VALUE[CheckBarNumber(seekBar.getId())]);
        }

        @Override
        public void onStartTrackingTouch(SeekBar seekBar) {
            // Do nothing
        }

        @Override
        public void onStopTrackingTouch(SeekBar seekBar) {
            // Do nothing
        }

        private void updateValue(int progress, int iMin) {
            mValueDisplay.setText(String.format("%d", (int) progress + iMin));
        }

        public void SetNewValue(int iValue, int iPos) {
            mOriginal = iValue - MIN_VALUE[iPos];
            reset(iPos);
        }

    }

        public void onClick(View v) {
                switch(v.getId()){
                case R.id.btnLulzDefault:
                        SetDefaultSettings();
                        break;
                case R.id.btnLulzBatt:
                        SetBatterySaveSettings();
                        break;
                case R.id.btnLulzSpeed:
                        SetSpeedUpSettings();
                        break;
                }
        }

        private void SetSpeedUpSettings() {
                mSeekBars[0].SetNewValue(60, 0);
                mSeekBars[1].SetNewValue(4, 1);
                mSeekBars[2].SetNewValue(1, 2);
                mSeekBars[3].SetNewValue(10000, 3);
                mSeekBars[4].SetNewValue(50000, 4);
        }

        private void SetBatterySaveSettings() {
                mSeekBars[0].SetNewValue(90, 0);
                mSeekBars[1].SetNewValue(1, 1);
                mSeekBars[2].SetNewValue(2, 2);
                mSeekBars[3].SetNewValue(50000, 3);
                mSeekBars[4].SetNewValue(40000, 4);
        }

        private void SetDefaultSettings() {
                mSeekBars[0].SetNewValue(60, 0);
                mSeekBars[1].SetNewValue(1, 1);
                mSeekBars[2].SetNewValue(1, 2);
                mSeekBars[3].SetNewValue(20000, 3);
                mSeekBars[4].SetNewValue(35000, 4);
        }

}
