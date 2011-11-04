package com.cyanogenmod.CrespoParts;

import android.os.Bundle;
import android.preference.ListPreference;
import android.preference.PreferenceActivity;

public class CrespoParts extends PreferenceActivity  {

    public static final String KEY_COLOR_TUNING = "color_tuning";
    public static final String KEY_BACKLIGHT_TIMEOUT = "backlight_timeout";
    public static final String KEY_BLINK_TIMEOUT = "blink_timeout";

    private ColorTuningPreference mColorTuning;
    private ListPreference mBacklightTimeout;
    private ListPreference mBlinkTimeout;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        addPreferencesFromResource(R.xml.main);

        mColorTuning = (ColorTuningPreference) findPreference(KEY_COLOR_TUNING);
        mColorTuning.setEnabled(ColorTuningPreference.isSupported());

        mBacklightTimeout = (ListPreference) findPreference(KEY_BACKLIGHT_TIMEOUT);
        mBacklightTimeout.setEnabled(TouchKeyBacklightTimeout.isSupported());
        mBacklightTimeout.setOnPreferenceChangeListener(new TouchKeyBacklightTimeout());

        mBlinkTimeout = (ListPreference) findPreference(KEY_BLINK_TIMEOUT);
        mBlinkTimeout.setEnabled(TouchKeyBacklightTimeout.isSupported());
        mBlinkTimeout.setOnPreferenceChangeListener(new TouchKeyBlinkTimeout());

    }

}
