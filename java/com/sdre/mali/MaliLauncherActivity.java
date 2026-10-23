package com.sdre.mali;

import android.app.Activity;
import android.content.ComponentName;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/** A local-only, experimental driver selector. It does not bypass game checks. */
public final class MaliLauncherActivity extends Activity {
    private static final int MODE_SYSTEM = 0;
    private static final int MODE_PANVK = 1;
    private static final int ID_PANVK = 1001;
    private static final int ID_SYSTEM = 1002;
    private static final int LOG_LIMIT = 120000;
    private static final int BACKGROUND = 0xff121820;
    private static final int TEXT = 0xffe8edf4;

    private SharedPreferences preferences;
    private RadioGroup drivers;
    private RadioButton panvk;
    private RadioButton system;
    private Button checkButton;
    private Button startButton;
    private Button shareButton;
    private ProgressBar progress;
    private TextView status;
    private TextView logView;
    private File diagnosticsFile;
    private String logText = "";
    private boolean busy;

    @Override public void onCreate(Bundle state) {
        setTheme(android.R.style.Theme_Holo_NoActionBar);
        super.onCreate(state);
        preferences = getSharedPreferences("mali_driver", MODE_PRIVATE);
        diagnosticsFile = new File(getFilesDir(), "mali-launcher-diagnostics.txt");
        logText = readPreviousLog();
        buildUi();
        appendLog("Launcher opened\n" + deviceDetails());
        File lastNative = new File(getFilesDir(), "mali-driver-diagnostics.txt");
        if (lastNative.isFile()) {
            FileInputStream input = null;
            try {
                input = new FileInputStream(lastNative);
                byte[] buffer = new byte[24576];
                int size = input.read(buffer);
                if (size > 0) appendLog("Last native diagnostic report (may be from a previous run):\n"
                        + new String(buffer, 0, size, "UTF-8"));
            } catch (Exception ignored) {
            } finally {
                if (input != null) try { input.close(); } catch (Exception ignored) { }
            }
        }
    }

    private void buildUi() {
        getWindow().setBackgroundDrawableResource(android.R.color.black);
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(BACKGROUND);
        LinearLayout body = new LinearLayout(this);
        body.setOrientation(LinearLayout.VERTICAL);
        body.setPadding(dp(20), dp(24), dp(20), dp(24));
        scroll.addView(body, new ScrollView.LayoutParams(-1, -2));
        setContentView(scroll);

        TextView title = text("Sleeping Dogs", 27, TEXT);
        title.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        body.addView(title);
        TextView subtitle = text("Mali driver test build", 18, 0xff9dc7ff);
        add(body, subtitle, 4);
        TextView warning = text("Experimental and untested on Oppo Reno 14 / Mali-G615 MC6. "
                + "Playability is not verified. The original game's Vulkan feature checks remain active.",
                15, 0xffffce89);
        add(body, warning, 18);
        add(body, text("Original game files are required in Internal storage/SleepingDogs. "
                + "Choose a Vulkan driver, check it, then start the game.", 15, TEXT), 12);

        TextView driverTitle = text("Vulkan driver", 18, TEXT);
        driverTitle.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        add(body, driverTitle, 22);
        drivers = new RadioGroup(this);
        drivers.setOrientation(RadioGroup.VERTICAL);
        panvk = new RadioButton(this);
        panvk.setId(ID_PANVK);
        panvk.setText("Bundled PanVK RC3 (experimental)");
        panvk.setTextColor(TEXT);
        panvk.setTextSize(16);
        drivers.addView(panvk, new RadioGroup.LayoutParams(-1, -2));
        system = new RadioButton(this);
        system.setId(ID_SYSTEM);
        system.setText("Phone system Vulkan driver");
        system.setTextColor(TEXT);
        system.setTextSize(16);
        drivers.addView(system, new RadioGroup.LayoutParams(-1, -2));
        int saved = preferences.getInt("mode", MODE_PANVK);
        drivers.check(saved == MODE_SYSTEM ? ID_SYSTEM : ID_PANVK);
        drivers.setOnCheckedChangeListener(new RadioGroup.OnCheckedChangeListener() {
            @Override public void onCheckedChanged(RadioGroup group, int checkedId) {
                preferences.edit().putInt("mode", selectedMode()).apply();
                status.setText("Selected: " + driverName(selectedMode()));
            }
        });
        add(body, drivers, 8);

        checkButton = button("Check selected driver");
        checkButton.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View view) { runNative(false); }
        });
        add(body, checkButton, 18);
        startButton = button("Start game");
        startButton.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View view) { runNative(true); }
        });
        add(body, startButton, 8);
        shareButton = button("Share diagnostics");
        shareButton.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View view) { shareDiagnostics(); }
        });
        add(body, shareButton, 8);

        progress = new ProgressBar(this);
        progress.setVisibility(View.GONE);
        LinearLayout.LayoutParams progressParams = new LinearLayout.LayoutParams(dp(32), dp(32));
        progressParams.topMargin = dp(14);
        body.addView(progress, progressParams);
        status = text("Selected: " + driverName(selectedMode()), 15, 0xff9dc7ff);
        add(body, status, 14);
        add(body, text("Diagnostics stay on this phone until you choose to share them.",
                13, 0xffa5b2c3), 14);
        logView = text(logText.length() == 0 ? "No diagnostics yet." : logText, 12, 0xffc8d2df);
        logView.setTypeface(Typeface.MONOSPACE);
        logView.setTextIsSelectable(true);
        logView.setPadding(dp(10), dp(10), dp(10), dp(10));
        logView.setBackgroundColor(0xff0a0f15);
        add(body, logView, 12);
    }

    private void runNative(final boolean launch) {
        if (busy) return;
        final int mode = selectedMode();
        final String nativeLibDir = getApplicationInfo().nativeLibraryDir;
        final String filesDir = getFilesDir().getAbsolutePath();
        setBusy(true);
        status.setText((launch ? "Configuring " : "Checking ") + driverName(mode) + "...");
        appendLog((launch ? "Configure before game start: " : "Driver check: ") + driverName(mode));
        new Thread(new Runnable() {
            @Override public void run() {
                String nativeResult;
                try {
                    nativeResult = launch
                            ? MaliBridge.configure(nativeLibDir, filesDir, mode)
                            : MaliBridge.probe(nativeLibDir, filesDir, mode);
                    if (nativeResult == null || nativeResult.trim().length() == 0) {
                        nativeResult = "ERROR: Native driver helper returned no result.";
                    }
                } catch (Throwable failure) {
                    nativeResult = "ERROR: " + failure.getClass().getName() + ": "
                            + String.valueOf(failure.getMessage());
                }
                final String result = nativeResult;
                runOnUiThread(new Runnable() {
                    @Override public void run() {
                        if (isFinishing()) return;
                        setBusy(false);
                        appendLog(result);
                        if (!launch) {
                            status.setText("Driver check finished. Read the diagnostics below.");
                        } else if (isSuccess(result)) {
                            startOriginalGame();
                        } else {
                            status.setText("Driver setup failed. Read or share the diagnostics below.");
                        }
                    }
                });
            }
        }, "MaliDriverCheck").start();
    }

    /** Native configure must explicitly acknowledge success before game launch. */
    private static boolean isSuccess(String result) {
        String value = result.trim();
        return value.equals("OK") || value.startsWith("OK:")
                || value.startsWith("OK ") || value.startsWith("OK\n");
    }

    private void startOriginalGame() {
        try {
            Intent game = new Intent();
            game.setComponent(new ComponentName(getPackageName(), "com.mod.MainActivity"));
            appendLog("Opening original game activity with its original feature checks.");
            startActivity(game);
            status.setText("Game opened. If it reports an error, return here to share diagnostics.");
        } catch (Throwable failure) {
            appendLog("ERROR: Could not open original game: " + failure.toString());
            status.setText("Could not open the game. Read the diagnostics below.");
        }
    }

    private void setBusy(boolean value) {
        busy = value;
        checkButton.setEnabled(!value);
        startButton.setEnabled(!value);
        shareButton.setEnabled(!value);
        panvk.setEnabled(!value);
        system.setEnabled(!value);
        progress.setVisibility(value ? View.VISIBLE : View.GONE);
    }

    @Override public void onBackPressed() {
        if (busy) {
            status.setText("Driver operation is running. Please wait for it to finish.");
            return;
        }
        super.onBackPressed();
    }

    private int selectedMode() {
        return drivers.getCheckedRadioButtonId() == ID_SYSTEM ? MODE_SYSTEM : MODE_PANVK;
    }

    private static String driverName(int mode) {
        return mode == MODE_SYSTEM ? "phone system Vulkan" : "bundled PanVK RC3";
    }

    private String deviceDetails() {
        return "Manufacturer: " + Build.MANUFACTURER
                + "\nModel: " + Build.MODEL
                + "\nDevice: " + Build.DEVICE
                + "\nAndroid: " + Build.VERSION.RELEASE + " (API " + Build.VERSION.SDK_INT + ")"
                + "\nABI: " + Build.CPU_ABI + " / " + Build.CPU_ABI2
                + "\nNative libraries: " + getApplicationInfo().nativeLibraryDir
                + "\nApp files: " + getFilesDir().getAbsolutePath()
                + "\nSelected driver: " + driverName(selectedMode());
    }

    private void appendLog(String message) {
        String time = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(new Date());
        logText += (logText.length() == 0 ? "" : "\n\n") + "[" + time + "]\n" + message;
        if (logText.length() > LOG_LIMIT) {
            logText = "[Older diagnostics omitted]\n" + logText.substring(logText.length() - LOG_LIMIT);
        }
        if (logView != null) logView.setText(logText);
        FileOutputStream out = null;
        try {
            out = new FileOutputStream(diagnosticsFile);
            out.write(logText.getBytes("UTF-8"));
        } catch (Exception failure) {
            if (logView != null) logView.setText(logText + "\n\nCould not save diagnostics: " + failure.toString());
        } finally {
            if (out != null) try { out.close(); } catch (Exception ignored) { }
        }
    }

    private String readPreviousLog() {
        FileInputStream in = null;
        try {
            if (!diagnosticsFile.isFile()) return "";
            in = new FileInputStream(diagnosticsFile);
            long skipped = Math.max(0, diagnosticsFile.length() - LOG_LIMIT);
            while (skipped > 0) {
                long count = in.skip(skipped);
                if (count <= 0) break;
                skipped -= count;
            }
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            byte[] buffer = new byte[4096];
            int count;
            while ((count = in.read(buffer)) != -1 && bytes.size() < LOG_LIMIT) {
                bytes.write(buffer, 0, Math.min(count, LOG_LIMIT - bytes.size()));
            }
            return new String(bytes.toByteArray(), "UTF-8");
        } catch (Exception failure) {
            return "Could not read previous diagnostics: " + failure.toString();
        } finally {
            if (in != null) try { in.close(); } catch (Exception ignored) { }
        }
    }

    private void shareDiagnostics() {
        try {
            Intent share = new Intent(Intent.ACTION_SEND);
            share.setType("text/plain");
            share.putExtra(Intent.EXTRA_SUBJECT, "Sleeping Dogs Mali driver diagnostics");
            share.putExtra(Intent.EXTRA_TEXT, "Sleeping Dogs Mali driver test build\n"
                    + "Experimental; playability is not verified.\n\n" + deviceDetails()
                    + "\n\n" + logText);
            startActivity(Intent.createChooser(share, "Share diagnostics"));
        } catch (Exception failure) {
            appendLog("ERROR: Could not open sharing: " + failure.toString());
            status.setText("Sharing is unavailable. You can select and copy the diagnostics below.");
        }
    }

    private TextView text(String value, int size, int color) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(size);
        view.setTextColor(color);
        return view;
    }

    private Button button(String label) {
        Button button = new Button(this);
        button.setText(label);
        button.setTextSize(16);
        button.setTextColor(Color.WHITE);
        button.setBackgroundColor(0xff2b3b51);
        button.setPadding(dp(12), dp(14), dp(12), dp(14));
        button.setMinHeight(dp(50));
        return button;
    }

    private void add(LinearLayout parent, View child, int topMargin) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        params.topMargin = dp(topMargin);
        parent.addView(child, params);
    }

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density + 0.5f);
    }
}
