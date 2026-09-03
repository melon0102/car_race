package com.revolt.game;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;

import java.util.List;

/**
 * Native Android front end: race setup (mode, track, car, options) with
 * persisted choices, replacing the 1999 keyboard menus. Launches the game
 * (MainActivity) with the chosen configuration as intent extras.
 */
public class LauncherActivity extends Activity {

    static final String EXTRA_LEVEL_DIR = "levelDir";
    static final String EXTRA_CAR_ID = "carId";
    static final String EXTRA_GAME_TYPE = "gameType";   // 0 = time trial, 1 = race vs cpu
    static final String EXTRA_REVERSED = "reversed";
    static final String EXTRA_MIRRORED = "mirrored";
    static final String EXTRA_NUM_CPUS = "numCpus";   // opponents in race mode (grid holds 12 cars)
    static final String EXTRA_TILT = "tiltSteer";     // steer by tilting the device
    static final String EXTRA_NUM_LAPS = "numLaps";   // race length (default 3; the 1999 build hardcoded 5)

    private static final String PREFS = "race_setup";

    private static final int BG = 0xFF101418;
    private static final int PANEL = 0xFF1C232B;
    private static final int ACCENT = 0xFFE53935;      // Re-Volt red
    private static final int TEXT = 0xFFECEFF1;
    private static final int TEXT_DIM = 0xFF90A4AE;

    private LinearLayout content;
    private ProgressBar progress;

    private Spinner trackSpinner;
    private Spinner carSpinner;
    private Spinner cpuSpinner;
    private Spinner lapsSpinner;
    private RadioButton modeTrial;
    private CheckBox reversedBox;
    private CheckBox mirroredBox;
    private CheckBox tiltBox;

    private List<GameData.Track> tracks;
    private List<GameData.Car> cars;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(BG);
        scroll.setFillViewport(true);

        content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(24);
        content.setPadding(pad, pad, pad, pad);
        scroll.addView(content);
        setContentView(scroll);

        TextView title = new TextView(this);
        title.setText("RE-VOLT");
        title.setTextColor(ACCENT);
        title.setTextSize(42);
        title.setTypeface(Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD_ITALIC));
        title.setGravity(Gravity.CENTER_HORIZONTAL);
        content.addView(title);

        TextView subtitle = new TextView(this);
        subtitle.setText("radio controlled racing");
        subtitle.setTextColor(TEXT_DIM);
        subtitle.setGravity(Gravity.CENTER_HORIZONTAL);
        subtitle.setPadding(0, 0, 0, dp(20));
        content.addView(subtitle);

        progress = new ProgressBar(this);
        LinearLayout.LayoutParams plp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        plp.gravity = Gravity.CENTER_HORIZONTAL;
        plp.topMargin = dp(40);
        progress.setLayoutParams(plp);
        content.addView(progress);

        // first run unpacks 120MB of game data — do it off the UI thread
        new Thread(() -> {
            GameData.unpack(this);
            tracks = GameData.listTracks(this);
            cars = GameData.listCars(this);

            // Debug builds pin the panel to one known-good combination
            // (Toys in the Hood + RC Bandit) so build-tests always exercise
            // the same content. All assets stay packaged; release builds
            // list everything.
            boolean debugBuild = (getApplicationInfo().flags
                    & android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) != 0;
            if (debugBuild) {
                tracks.removeIf(t -> !t.dir.equalsIgnoreCase("nhood1"));
                cars.removeIf(c -> c.id != 0);
            }

            runOnUiThread(this::buildForm);
        }, "gamedata-unpack").start();
    }

    private void buildForm() {
        content.removeView(progress);
        SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);

        // game mode
        addLabel("MODE");
        RadioGroup mode = new RadioGroup(this);
        mode.setOrientation(RadioGroup.HORIZONTAL);
        modeTrial = new RadioButton(this);
        modeTrial.setText("Time Trial");
        modeTrial.setTextColor(TEXT);
        RadioButton modeRace = new RadioButton(this);
        modeRace.setText("Race vs CPU");
        modeRace.setTextColor(TEXT);
        mode.addView(modeTrial);
        mode.addView(modeRace);
        if (prefs.getInt(EXTRA_GAME_TYPE, 1) == 1) modeRace.setChecked(true);   // default: Race vs CPU
        else modeTrial.setChecked(true);
        addPanel(mode);

        // track
        addLabel("TRACK");
        trackSpinner = new Spinner(this);
        ArrayAdapter<GameData.Track> trackAdapter = new ArrayAdapter<>(
                this, android.R.layout.simple_spinner_item, tracks);
        trackAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        trackSpinner.setAdapter(trackAdapter);
        selectTrack(prefs.getString(EXTRA_LEVEL_DIR, "nhood1"));
        addPanel(trackSpinner);

        // car
        addLabel("CAR");
        carSpinner = new Spinner(this);
        ArrayAdapter<GameData.Car> carAdapter = new ArrayAdapter<>(
                this, android.R.layout.simple_spinner_item, cars);
        carAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        carSpinner.setAdapter(carAdapter);
        int savedCar = prefs.getInt(EXTRA_CAR_ID, 0);
        for (int i = 0; i < cars.size(); i++) {
            if (cars.get(i).id == savedCar) { carSpinner.setSelection(i); break; }
        }
        addPanel(carSpinner);

        // opponents (race mode)
        addLabel("OPPONENTS");
        cpuSpinner = new Spinner(this);
        Integer[] cpuCounts = {1, 2, 3, 4, 5, 6, 7, 11};
        ArrayAdapter<Integer> cpuAdapter = new ArrayAdapter<>(
                this, android.R.layout.simple_spinner_item, cpuCounts);
        cpuAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        cpuSpinner.setAdapter(cpuAdapter);
        int savedCpus = prefs.getInt(EXTRA_NUM_CPUS, 7);
        for (int i = 0; i < cpuCounts.length; i++) {
            if (cpuCounts[i] == savedCpus) { cpuSpinner.setSelection(i); break; }
        }
        addPanel(cpuSpinner);

        // laps
        addLabel("LAPS");
        lapsSpinner = new Spinner(this);
        Integer[] lapCounts = {1, 2, 3, 4, 5, 6, 8, 10};
        ArrayAdapter<Integer> lapsAdapter = new ArrayAdapter<>(
                this, android.R.layout.simple_spinner_item, lapCounts);
        lapsAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        lapsSpinner.setAdapter(lapsAdapter);
        int savedLaps = prefs.getInt(EXTRA_NUM_LAPS, 3);
        for (int i = 0; i < lapCounts.length; i++) {
            if (lapCounts[i] == savedLaps) { lapsSpinner.setSelection(i); break; }
        }
        addPanel(lapsSpinner);

        // options
        addLabel("OPTIONS");
        LinearLayout opts = new LinearLayout(this);
        opts.setOrientation(LinearLayout.HORIZONTAL);
        reversedBox = new CheckBox(this);
        reversedBox.setText("Reversed");
        reversedBox.setTextColor(TEXT);
        reversedBox.setChecked(prefs.getBoolean(EXTRA_REVERSED, false));
        mirroredBox = new CheckBox(this);
        mirroredBox.setText("Mirrored");
        mirroredBox.setTextColor(TEXT);
        mirroredBox.setChecked(prefs.getBoolean(EXTRA_MIRRORED, false));
        tiltBox = new CheckBox(this);
        tiltBox.setText("Tilt steering");
        tiltBox.setTextColor(TEXT);
        tiltBox.setChecked(prefs.getBoolean(EXTRA_TILT, true));
        opts.addView(reversedBox);
        opts.addView(mirroredBox);
        opts.addView(tiltBox);
        addPanel(opts);

        // start
        Button start = new Button(this);
        start.setText("START RACE");
        start.setTextSize(20);
        start.setTypeface(Typeface.DEFAULT_BOLD);
        start.setTextColor(Color.WHITE);
        start.setBackgroundColor(ACCENT);
        LinearLayout.LayoutParams slp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(56));
        slp.topMargin = dp(28);
        start.setLayoutParams(slp);
        start.setOnClickListener(v -> startRace());
        content.addView(start);

        TextView hint = new TextView(this);
        hint.setText("Controls: steer with the bottom-left arrows or by tilting "
                + "the device, accelerate/brake with the right-edge arrows, tap "
                + "the car button to flip upright, tap the pickup box to fire.");
        hint.setTextColor(TEXT_DIM);
        hint.setPadding(0, dp(16), 0, 0);
        content.addView(hint);
    }

    private void startRace() {
        GameData.Track track = (GameData.Track) trackSpinner.getSelectedItem();
        GameData.Car car = (GameData.Car) carSpinner.getSelectedItem();
        if (track == null || car == null) return;
        int gameType = modeTrial.isChecked() ? 0 : 1;
        Integer cpus = (Integer) cpuSpinner.getSelectedItem();
        int numCpus = (cpus != null) ? cpus : 7;
        Integer laps = (Integer) lapsSpinner.getSelectedItem();
        int numLaps = (laps != null) ? laps : 3;

        getSharedPreferences(PREFS, MODE_PRIVATE).edit()
                .putString(EXTRA_LEVEL_DIR, track.dir)
                .putInt(EXTRA_CAR_ID, car.id)
                .putInt(EXTRA_GAME_TYPE, gameType)
                .putBoolean(EXTRA_REVERSED, reversedBox.isChecked())
                .putBoolean(EXTRA_MIRRORED, mirroredBox.isChecked())
                .putInt(EXTRA_NUM_CPUS, numCpus)
                .putInt(EXTRA_NUM_LAPS, numLaps)
                .putBoolean(EXTRA_TILT, tiltBox.isChecked())
                .apply();

        Intent intent = new Intent(this, MainActivity.class);
        intent.putExtra(EXTRA_LEVEL_DIR, track.dir);
        intent.putExtra(EXTRA_CAR_ID, car.id);
        intent.putExtra(EXTRA_GAME_TYPE, gameType);
        intent.putExtra(EXTRA_REVERSED, reversedBox.isChecked());
        intent.putExtra(EXTRA_MIRRORED, mirroredBox.isChecked());
        intent.putExtra(EXTRA_NUM_CPUS, numCpus);
        intent.putExtra(EXTRA_NUM_LAPS, numLaps);
        intent.putExtra(EXTRA_TILT, tiltBox.isChecked());
        startActivity(intent);
    }

    // ------------------------------------------------------------ ui helpers

    private void addLabel(String text) {
        TextView label = new TextView(this);
        label.setText(text);
        label.setTextColor(ACCENT);
        label.setTextSize(13);
        label.setTypeface(Typeface.DEFAULT_BOLD);
        label.setPadding(0, dp(18), 0, dp(6));
        content.addView(label);
    }

    private void addPanel(View inner) {
        inner.setBackgroundColor(PANEL);
        int pad = dp(8);
        inner.setPadding(pad, pad, pad, pad);
        content.addView(inner, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
    }

    private void selectTrack(String dir) {
        for (int i = 0; i < tracks.size(); i++) {
            if (tracks.get(i).dir.equalsIgnoreCase(dir)) { trackSpinner.setSelection(i); return; }
        }
    }

    private int dp(int v) {
        return (int) (v * getResources().getDisplayMetrics().density + 0.5f);
    }
}
