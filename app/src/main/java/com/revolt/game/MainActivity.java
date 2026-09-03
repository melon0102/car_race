package com.revolt.game;

import android.app.Activity;
import android.graphics.PixelFormat;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Bundle;
import android.util.Log;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;

/**
 * SDL-style shell: a plain Activity owning a SurfaceView whose Surface is
 * handed to the native game thread over JNI (the architecture RVGL uses,
 * which works everywhere including emulators). Input is forwarded from the
 * view callbacks as simple JNI calls.
 */
public class MainActivity extends Activity implements SurfaceHolder.Callback {

    static {
        System.loadLibrary("revolt");
    }

    private static native void nativeSetDataDir(String dir);
    private static native void nativeConfigure(String levelDir, int carId, int gameType,
                                               boolean reversed, boolean mirrored,
                                               int numCpuCars, int numLaps);
    private static native void nativeSurfaceChanged(Surface surface, int w, int h);
    private static native void nativeSurfaceDestroyed();
    private static native void nativeTouchReset();
    private static native void nativeTouchPoint(float x, float y);
    private static native void nativeKey(int keyCode, int down);
    private static native void nativeAxes(float sx, float gas, float brake);
    private static native void nativeTilt(float steer);

    private static final String TAG = "revolt-java";

    private SurfaceView surfaceView;

    // ---- tilt steering: gravity along the screen's x axis -> [-1,1] ----
    // Works alongside the touch buttons and gamepad; the native layer just
    // merges it as one more steer source.

    /** ±FULL_LOCK_G of gravity along screen-x = full steer (~24 degrees). */
    private static final float TILT_FULL_LOCK_G = 4.0f;
    private static final float TILT_FILTER = 0.25f;   // low-pass per sample

    private SensorManager sensorManager;
    private Sensor accelerometer;
    private boolean tiltEnabled;
    private float tiltFiltered;

    private final SensorEventListener tiltListener = new SensorEventListener() {
        @Override
        public void onSensorChanged(SensorEvent ev) {
            // gravity component along screen-right, per display rotation
            // (sensorLandscape allows both landscape orientations, and on
            // landscape-natural devices ROTATION_0/180 appear too)
            float gRight;
            switch (getWindowManager().getDefaultDisplay().getRotation()) {
                case Surface.ROTATION_90:  gRight =  ev.values[1]; break;
                case Surface.ROTATION_270: gRight = -ev.values[1]; break;
                case Surface.ROTATION_180: gRight = -ev.values[0]; break;
                default:                   gRight =  ev.values[0]; break;
            }
            // right edge dipped => screen-right points below the horizon =>
            // its gravity reading goes negative => positive steer
            float steer = -gRight / TILT_FULL_LOCK_G;
            if (steer > 1f) steer = 1f;
            if (steer < -1f) steer = -1f;
            tiltFiltered += TILT_FILTER * (steer - tiltFiltered);
            nativeTilt(tiltFiltered);
        }

        @Override
        public void onAccuracyChanged(Sensor sensor, int accuracy) { }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        GameData.unpack(this);   // no-op when the launcher already did it

        nativeConfigure(
                getIntent().getStringExtra(LauncherActivity.EXTRA_LEVEL_DIR),
                getIntent().getIntExtra(LauncherActivity.EXTRA_CAR_ID, 0),
                getIntent().getIntExtra(LauncherActivity.EXTRA_GAME_TYPE, 1),
                getIntent().getBooleanExtra(LauncherActivity.EXTRA_REVERSED, false),
                getIntent().getBooleanExtra(LauncherActivity.EXTRA_MIRRORED, false),
                getIntent().getIntExtra(LauncherActivity.EXTRA_NUM_CPUS, 7),
                getIntent().getIntExtra(LauncherActivity.EXTRA_NUM_LAPS, 3));
        nativeSetDataDir(GameData.root(this).getAbsolutePath());

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        surfaceView = new SurfaceView(this);
        surfaceView.getHolder().addCallback(this);
        surfaceView.getHolder().setFormat(PixelFormat.RGBA_8888);
        surfaceView.setFocusable(true);
        surfaceView.setFocusableInTouchMode(true);

        surfaceView.setOnTouchListener((v, ev) -> {
            int action = ev.getActionMasked();
            nativeTouchReset();
            if (action != MotionEvent.ACTION_UP && action != MotionEvent.ACTION_CANCEL) {
                int lifted = (action == MotionEvent.ACTION_POINTER_UP)
                        ? ev.getActionIndex() : -1;
                float w = Math.max(1, v.getWidth());
                float h = Math.max(1, v.getHeight());
                for (int p = 0; p < ev.getPointerCount(); p++) {
                    if (p == lifted) continue;
                    nativeTouchPoint(ev.getX(p) / w, ev.getY(p) / h);
                }
            }
            return true;
        });

        surfaceView.setOnGenericMotionListener((v, ev) -> {
            if ((ev.getSource() & InputDevice.SOURCE_JOYSTICK) != 0) {
                float sx = ev.getAxisValue(MotionEvent.AXIS_X);
                float gas = ev.getAxisValue(MotionEvent.AXIS_GAS);
                float brake = ev.getAxisValue(MotionEvent.AXIS_BRAKE);
                if (gas == 0f) gas = ev.getAxisValue(MotionEvent.AXIS_RTRIGGER);
                if (brake == 0f) brake = ev.getAxisValue(MotionEvent.AXIS_LTRIGGER);
                nativeAxes(sx, gas, brake);
                return true;
            }
            return false;
        });

        setContentView(surfaceView);
        surfaceView.requestFocus();
        hideSystemBars();

        tiltEnabled = getIntent().getBooleanExtra(LauncherActivity.EXTRA_TILT, true);
        if (tiltEnabled) {
            sensorManager = (SensorManager) getSystemService(SENSOR_SERVICE);
            accelerometer = sensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
            if (accelerometer == null) tiltEnabled = false;   // no sensor: buttons only
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (tiltEnabled) {
            sensorManager.registerListener(tiltListener, accelerometer,
                    SensorManager.SENSOR_DELAY_GAME);
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (tiltEnabled) {
            sensorManager.unregisterListener(tiltListener);
            tiltFiltered = 0f;
            nativeTilt(0f);   // never leave a stale steer applied
        }
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        int action = event.getAction();
        // BACK leaves the race and returns to the launcher
        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK) {
            if (action == KeyEvent.ACTION_UP) {
                finish();
            }
            return true;
        }
        if (action == KeyEvent.ACTION_DOWN || action == KeyEvent.ACTION_UP) {
            nativeKey(event.getKeyCode(), action == KeyEvent.ACTION_DOWN ? 1 : 0);
        }
        return super.dispatchKeyEvent(event);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        // the game core has no teardown path (1999 code boots once per process);
        // this activity runs in its own :game process — end it so the next race
        // starts clean. The launcher process is unaffected.
        android.os.Process.killProcess(android.os.Process.myPid());
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        // wait for surfaceChanged (carries real size/format)
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        Log.i(TAG, "surfaceChanged " + width + "x" + height + " fmt=" + format);
        nativeSurfaceChanged(holder.getSurface(), width, height);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        nativeSurfaceDestroyed();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemBars();
        }
    }

    @SuppressWarnings("deprecation")
    private void hideSystemBars() {
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
    }

}
