package com.cyanogenmod.CrespoParts;

import android.content.Context;
import android.content.SharedPreferences;
import android.content.SharedPreferences.Editor;
import android.preference.DialogPreference;
import android.preference.PreferenceManager;
import android.util.AttributeSet;
import android.view.View;
import android.widget.SeekBar;
import android.widget.TextView;

/**
 * Special preference type that allows configuration of both the ring volume and
 * notification volume.
 */
public class GammaTuningPreference extends DialogPreference {

    private static final String TAG = "GAMMA...";

    enum Colors {
        RED,
        GREEN,
        BLUE
    };

    private static final int[] SEEKBAR_ID = new int[] {
        R.id.gamma_red_seekbar,
        R.id.gamma_green_seekbar,
        R.id.gamma_blue_seekbar
    };

    private static final int[] VALUE_DISPLAY_ID = new int[] {
        R.id.gamma_red_value,
        R.id.gamma_green_value,
        R.id.gamma_blue_value
    };

    private static final String[] FILE_PATH = new String[] {
        "/sys/class/misc/voodoo_color/red_v1_offset",
        "/sys/class/misc/voodoo_color/green_v1_offset",
        "/sys/class/misc/voodoo_color/blue_v1_offset"
    };

    private GammaSeekBar mSeekBars[] = new GammaSeekBar[3];

    private static final int MAX_VALUE = 80;

    // Track instances to know when to restore original color
    // (when the orientation changes, a new dialog is created before the old one is destroyed)
    private static int sInstances = 0;

    public GammaTuningPreference(Context context, AttributeSet attrs) {
        super(context, attrs);

        setDialogLayoutResource(R.layout.preference_dialog_gamma_tuning);
    }

    @Override
    protected void onBindDialogView(View view) {
        super.onBindDialogView(view);

        sInstances++;

        for (int i = 0; i < SEEKBAR_ID.length; i++) {
            SeekBar seekBar = (SeekBar) view.findViewById(SEEKBAR_ID[i]);
            TextView valueDisplay = (TextView) view.findViewById(VALUE_DISPLAY_ID[i]);
            mSeekBars[i] = new GammaSeekBar(seekBar, valueDisplay, FILE_PATH[i]);
        }
    }

    @Override
    protected void onDialogClosed(boolean positiveResult) {
        super.onDialogClosed(positiveResult);

        sInstances--;

        if (positiveResult) {
            for (GammaSeekBar csb : mSeekBars) {
                csb.save();
            }
        } else if (sInstances == 0) {
            for (GammaSeekBar csb : mSeekBars) {
                csb.reset();
            }
        }
    }

    /**
     * Restore screen color tuning from SharedPreferences. (Write to kernel.)
     * @param context       The context to read the SharedPreferences from
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
     * Check whether the running kernel supports color tuning or not.
     * @return              Whether color tuning is supported or not
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

    class GammaSeekBar implements SeekBar.OnSeekBarChangeListener {

        private String mFilePath;
        private int mOriginal;
        private SeekBar mSeekBar;
        private TextView mValueDisplay;

        public GammaSeekBar(SeekBar seekBar, TextView valueDisplay, String filePath) {
            mSeekBar = seekBar;
            mValueDisplay = valueDisplay;
            mFilePath = filePath;

            // Read original value
            SharedPreferences sharedPreferences = getSharedPreferences();
            mOriginal = sharedPreferences.getInt(mFilePath, MAX_VALUE);

            seekBar.setMax(MAX_VALUE);

            reset();
            seekBar.setOnSeekBarChangeListener(this);
        }

        public void reset() {
        int iValue;

            iValue = mOriginal+60;
            mSeekBar.setProgress(iValue);
            updateValue(mOriginal);
        }

        public void save() {
        int iValue;

            iValue = mSeekBar.getProgress()-60;
            Editor editor = getEditor();
            editor.putInt(mFilePath, iValue);
            editor.commit();
        }

        @Override
        public void onProgressChanged(SeekBar seekBar, int progress,
                boolean fromUser) {
        int iValue;

            iValue = progress-60;
            Utils.writeValue(mFilePath, String.valueOf((long) iValue));
            updateValue(iValue);
        }

        @Override
        public void onStartTrackingTouch(SeekBar seekBar) {
            // Do nothing
        }

        @Override
        public void onStopTrackingTouch(SeekBar seekBar) {
            // Do nothing
        }

        private void updateValue(int progress) {
            mValueDisplay.setText(String.format("%d",(int) progress ));
        }

    }

}
