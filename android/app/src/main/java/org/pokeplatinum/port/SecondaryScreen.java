package org.pokeplatinum.port;

import android.app.Activity;
import android.app.Presentation;
import android.content.Context;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Display;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

/**
 * Puts the DS bottom screen on a second physical display (the AYN Thor's lower panel,
 * or any other dual-screen device / external display).
 *
 * The Presentation hosts a SurfaceView whose Surface is handed to libntr, which renders
 * the bottom screen into it with the game's own GL context (sim_display.cpp). Touches on
 * the SurfaceView become DS touch screen input.
 */
final class SecondaryScreen implements DisplayManager.DisplayListener {
    private static final String TAG = "SecondaryScreen";

    private final Activity activity;
    private final DisplayManager displayManager;
    private ScreenPresentation presentation;
    private boolean wanted;

    SecondaryScreen(Activity activity) {
        this.activity = activity;
        this.displayManager = (DisplayManager) activity.getSystemService(Context.DISPLAY_SERVICE);
        displayManager.registerDisplayListener(this, new Handler(Looper.getMainLooper()));
    }

    void show() {
        wanted = true;
        update();
    }

    void hide() {
        wanted = false;
        dismiss();
    }

    void release() {
        hide();
        displayManager.unregisterDisplayListener(this);
    }

    private int ownDisplayId() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && activity.getDisplay() != null) {
            return activity.getDisplay().getDisplayId();
        }
        //noinspection deprecation
        return activity.getWindowManager().getDefaultDisplay().getDisplayId();
    }

    private Display pickDisplay() {
        int ownId = ownDisplayId();
        Display[] candidates = displayManager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION);
        if (candidates.length == 0) {
            // Some handhelds don't flag their second panel as a presentation display
            candidates = displayManager.getDisplays();
        }
        for (Display display : candidates) {
            int id = display.getDisplayId();
            if (id == ownId || id == Display.DEFAULT_DISPLAY) {
                continue;
            }
            if (!display.isValid() || display.getState() == Display.STATE_OFF) {
                continue;
            }
            if ((display.getFlags() & Display.FLAG_PRIVATE) != 0) {
                continue; // someone else's virtual display (screen recorders, casting apps)
            }
            return display;
        }
        return null;
    }

    private void update() {
        if (!wanted || activity.isFinishing() || activity.isDestroyed()) {
            dismiss();
            return;
        }

        Display target = pickDisplay();
        if (presentation != null && target != null
                && presentation.getDisplay().getDisplayId() == target.getDisplayId()) {
            return; // already there
        }

        dismiss();
        if (target == null) {
            return; // single screen: the game falls back to its normal layout
        }

        try {
            presentation = new ScreenPresentation(activity, target);
            presentation.show();
            Log.i(TAG, "Bottom screen on display " + target.getDisplayId() + " (" + target.getName() + ")");
        } catch (WindowManager.InvalidDisplayException | WindowManager.BadTokenException e) {
            // The display went away while we were choosing it; the listener will retry
            Log.w(TAG, "Could not show the bottom screen", e);
            presentation = null;
        }
    }

    private void dismiss() {
        if (presentation != null) {
            try {
                presentation.dismiss();
            } catch (RuntimeException ignored) {
                // already gone with its display
            }
            presentation = null;
        }
    }

    @Override
    public void onDisplayAdded(int displayId) {
        update();
    }

    @Override
    public void onDisplayRemoved(int displayId) {
        update();
    }

    @Override
    public void onDisplayChanged(int displayId) {
        update();
    }

    // Implemented in libntr (sim_display.cpp)
    static native void nativeSurfaceChanged(Surface surface);
    static native void nativeSurfaceDestroyed();
    static native void nativeTouch(boolean down, float x, float y);

    private static final class ScreenPresentation extends Presentation {
        private int touchPointerId = -1;

        ScreenPresentation(Context context, Display display) {
            super(context, display);
        }

        @Override
        protected void onCreate(Bundle savedInstanceState) {
            super.onCreate(savedInstanceState);

            Window window = getWindow();
            if (window != null) {
                // Not focusable: touching the lower panel must not pull keyboard/gamepad
                // focus away from the game activity on the top panel.
                window.addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                        | WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL
                        | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            }

            SurfaceView surfaceView = new SurfaceView(getContext());
            surfaceView.getHolder().addCallback(new SurfaceHolder.Callback() {
                @Override
                public void surfaceCreated(SurfaceHolder holder) {
                }

                @Override
                public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
                    nativeSurfaceChanged(holder.getSurface());
                }

                @Override
                public void surfaceDestroyed(SurfaceHolder holder) {
                    // Returns once the renderer has let go of the surface
                    nativeSurfaceDestroyed();
                }
            });
            surfaceView.setOnTouchListener(this::onTouch);
            setContentView(surfaceView);
            hideSystemBars();
        }

        private void hideSystemBars() {
            Window window = getWindow();
            if (window == null) {
                return;
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                WindowInsetsController controller = window.getInsetsController();
                if (controller != null) {
                    controller.hide(WindowInsets.Type.systemBars());
                    controller.setSystemBarsBehavior(
                            WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                }
            } else {
                //noinspection deprecation
                window.getDecorView().setSystemUiVisibility(
                        View.SYSTEM_UI_FLAG_FULLSCREEN
                                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
            }
        }

        // The DS has one stylus: follow the first finger down until it lifts
        private boolean onTouch(View view, MotionEvent event) {
            int action = event.getActionMasked();
            if (action == MotionEvent.ACTION_DOWN) {
                // Deliver moves as they happen instead of batching them per frame
                view.requestUnbufferedDispatch(event);
            }

            int index = event.getActionIndex();
            switch (action) {
                case MotionEvent.ACTION_DOWN:
                case MotionEvent.ACTION_POINTER_DOWN:
                    if (touchPointerId == -1) {
                        touchPointerId = event.getPointerId(index);
                        nativeTouch(true, event.getX(index), event.getY(index));
                    }
                    break;
                case MotionEvent.ACTION_MOVE: {
                    int pointerIndex = event.findPointerIndex(touchPointerId);
                    if (pointerIndex != -1) {
                        nativeTouch(true, event.getX(pointerIndex), event.getY(pointerIndex));
                    }
                    break;
                }
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_POINTER_UP:
                    if (event.getPointerId(index) == touchPointerId) {
                        nativeTouch(false, event.getX(index), event.getY(index));
                        touchPointerId = -1;
                    }
                    break;
                case MotionEvent.ACTION_CANCEL:
                    nativeTouch(false, 0, 0);
                    touchPointerId = -1;
                    break;
                default:
                    break;
            }
            return true;
        }
    }
}
