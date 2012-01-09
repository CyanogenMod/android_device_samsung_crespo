package com.cyanogenmod.settings.device;

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.SystemProperties;
import android.preference.Preference;
import android.preference.Preference.OnPreferenceChangeListener;
import android.preference.PreferenceManager;

public class Hspa implements OnPreferenceChangeListener {

    private static final String APK_FILE = "/system/app/SamsungServiceMode.apk";
    private static final String HSPA_PROP = "ro.crespoparts.rild.hspa";
    private static final String HSPA_PROP_ENABLED = "1";

    private Context mCtx;

    public Hspa(Context context) {
        mCtx = context;
    }

    public static boolean isSupported() {
        String mHspa = SystemProperties.get(HSPA_PROP,"0");
        if (mHspa.equals(HSPA_PROP_ENABLED)) {
            return true;
        } else {
            return false;
        }
    }

    /**
     * Restore HSPA setting from SharedPreferences. (Write to kernel.)
     * @param context       The context to read the SharedPreferences from
     */
    public static void restore(Context context) {
        if (!isSupported()) {
            return;
        }

        SharedPreferences sharedPrefs = PreferenceManager.getDefaultSharedPreferences(context);
        sendIntent(context, sharedPrefs.getString(DeviceSettings.KEY_HSPA, "23"));
    }

    @Override
    public boolean onPreferenceChange(Preference preference, Object newValue) {
        sendIntent(mCtx, (String) newValue);
        return true;
    }

    private static void sendIntent(Context context, String value) {
        Intent i = new Intent("com.cyanogenmod.SamsungServiceMode.EXECUTE");
        i.putExtra("sub_type", 20); // HSPA Setting
        i.putExtra("data", value);
        context.sendBroadcast(i);
    }
}
