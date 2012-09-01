package com.cyanogenmod.settings.device;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

public class Startup extends BroadcastReceiver {

    @Override
    public void onReceive(final Context context, final Intent bootintent) {
        GeneralFragmentActivity.restore(context);
        ColorTuningPreference.restore(context);
        bldTuningPreference.restore(context);
        GammaTuningPreference.restore(context);
        if (Hspa.isSupported()) {
            Hspa.restore(context);
        }
    }
}
