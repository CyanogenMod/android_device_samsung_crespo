package com.cyanogenmod.CrespoParts;

import android.os.Bundle;
import android.preference.ListPreference;
import android.preference.PreferenceActivity;
import android.preference.PreferenceCategory;
import android.preference.PreferenceScreen;

public class CrespoParts extends PreferenceActivity  {

    public static final String KEY_COLOR_TUNING = "color_tuning";
    public static final String KEY_GAMMA_TUNING = "gamma_tuning";
    public static final String KEY_BACKLIGHT_TIMEOUT = "backlight_timeout";
    public static final String KEY_BLINK_TIMEOUT = "blink_timeout";
    public static final String KEY_CATEGORY_RADIO = "category_radio";
    public static final String KEY_HSPA = "hspa";

    private ColorTuningPreference mColorTuning;
    private GammaTuningPreference mGammaTuning;
    private ListPreference mBacklightTimeout;
    private ListPreference mBlinkTimeout;
    private ListPreference mHspa;
    private PreferenceCategory mHsapCategory;
    private PreferenceScreen mPreferenceScreen;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        addPreferencesFromResource(R.xml.main);

        mColorTuning = (ColorTuningPreference) findPreference(KEY_COLOR_TUNING);
        mColorTuning.setEnabled(ColorTuningPreference.isSupported());

        mGammaTuning = (GammaTuningPreference) findPreference(KEY_GAMMA_TUNING);
        mGammaTuning.setEnabled(ColorTuningPreference.isSupported());

        mBacklightTimeout = (ListPreference) findPreference(KEY_BACKLIGHT_TIMEOUT);
        mBacklightTimeout.setEnabled(TouchKeyBacklightTimeout.isSupported());
        mBacklightTimeout.setOnPreferenceChangeListener(new TouchKeyBacklightTimeout());

        mBlinkTimeout = (ListPreference) findPreference(KEY_BLINK_TIMEOUT);
        mBlinkTimeout.setEnabled(TouchKeyBacklightTimeout.isSupported());
        mBlinkTimeout.setOnPreferenceChangeListener(new TouchKeyBlinkTimeout());

        mHspa = (ListPreference) findPreference(KEY_HSPA);

        if (Hspa.isSupported()) {
            mHspa.setEnabled(true);
            mHspa.setOnPreferenceChangeListener(new Hspa(this));
        } else {
            mHsapCategory = (PreferenceCategory) findPreference(KEY_CATEGORY_RADIO);
            mPreferenceScreen = getPreferenceScreen();

            mHspa.setEnabled(false);
            mHsapCategory.removePreference(mHspa);
            mPreferenceScreen.removePreference(mHsapCategory);
        }
    }
}
