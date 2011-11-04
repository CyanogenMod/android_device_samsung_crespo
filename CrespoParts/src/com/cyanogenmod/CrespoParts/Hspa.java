package com.cyanogenmod.CrespoParts;

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.preference.Preference;
import android.preference.Preference.OnPreferenceChangeListener;
import android.preference.PreferenceManager;

public class Hspa implements OnPreferenceChangeListener {

    private static final String APK_FILE = "/system/app/SamsungServiceMode.apk";
    private Context mCtx;

    public Hspa(Context context) {
        mCtx = context;
    }

    public static boolean isSupported() {
        return Utils.fileExists(APK_FILE);
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
        sendIntent(context, sharedPrefs.getString(CrespoParts.KEY_HSPA, "23"));
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