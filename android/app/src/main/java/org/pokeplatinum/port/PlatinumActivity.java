package org.pokeplatinum.port;

import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;
import android.view.Display;
import android.view.Window;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * SDL activity hosting the game (libmain.so). Adds what the native side can't do alone:
 * the lower-screen Presentation on dual-screen devices, and importing the player's ROM
 * through the system file picker.
 */
public class PlatinumActivity extends SDLActivity {
    private static final String TAG = "PlatinumActivity";
    private static final int REQUEST_ROM = 0x504C; // "PL"
    private static final int REQUEST_OPEN_DOCUMENT = 0x504D;
    private static final int REQUEST_CREATE_DOCUMENT = 0x504E;

    /** File waiting to be written to the document the player is choosing (export). */
    private String pendingExportSource;

    private SecondaryScreen secondaryScreen;

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        preferSixtyHertz();
        installBundledFiles();
        secondaryScreen = new SecondaryScreen(this);
    }

    /**
     * The game paces itself to 60 fps. On 90/120 Hz panels (the Thor's top screen) ask for
     * the 60 Hz mode at the current resolution so every frame lands on a vsync.
     */
    private void preferSixtyHertz() {
        //noinspection deprecation
        Display display = getWindowManager().getDefaultDisplay();
        Display.Mode current = display.getMode();
        for (Display.Mode mode : display.getSupportedModes()) {
            if (Math.abs(mode.getRefreshRate() - 60f) < 1f
                    && mode.getPhysicalWidth() == current.getPhysicalWidth()
                    && mode.getPhysicalHeight() == current.getPhysicalHeight()) {
                Window window = getWindow();
                WindowManager.LayoutParams params = window.getAttributes();
                params.preferredDisplayModeId = mode.getModeId();
                window.setAttributes(params);
                return;
            }
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (secondaryScreen != null) {
            secondaryScreen.show();
        }
    }

    @Override
    protected void onPause() {
        if (secondaryScreen != null) {
            secondaryScreen.hide();
        }
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        if (secondaryScreen != null) {
            secondaryScreen.release();
            secondaryScreen = null;
        }
        super.onDestroy();
    }

    /** Where libntr keeps the extracted ROM, saves and config files (its working directory). */
    File dataDirectory() {
        File dir = getExternalFilesDir(null);
        return dir != null ? dir : getFilesDir();
    }

    /**
     * firmware.bin is the port's own placeholder for the DS Wi-Fi settings block (no real
     * firmware). It ships in the APK assets and is copied out once.
     */
    private void installBundledFiles() {
        File firmware = new File(dataDirectory(), "firmware.bin");
        if (firmware.exists()) {
            return;
        }
        try (InputStream in = getAssets().open("firmware.bin");
             OutputStream out = new FileOutputStream(firmware)) {
            copy(in, out);
        } catch (IOException e) {
            Log.w(TAG, "Could not install firmware.bin", e);
        }
    }

    /** Called from native code (SDL thread) when the ROM files still need extracting. */
    @SuppressWarnings("unused")
    public void requestRomImport() {
        runOnUiThread(() -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            // .nds has no registered MIME type, so offer every file
            intent.setType("*/*");
            try {
                startActivityForResult(intent, REQUEST_ROM);
            } catch (ActivityNotFoundException e) {
                Log.e(TAG, "No document picker available", e);
                nativeRomImportFinished(null);
            }
        });
    }

    /**
     * Called from native code (sim_files.cpp): let the player pick any file (a save to
     * import, for example). It is copied into imports/ in the data directory and the
     * copy's path reported through nativeDocumentPicked.
     */
    @SuppressWarnings("unused")
    public void pickDocument() {
        runOnUiThread(() -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            try {
                startActivityForResult(intent, REQUEST_OPEN_DOCUMENT);
            } catch (ActivityNotFoundException e) {
                Log.e(TAG, "No document picker available", e);
                nativeDocumentPicked(null);
            }
        });
    }

    /**
     * Called from native code: let the player choose where to save a copy of sourcePath
     * (a save to export). Reported through nativeDocumentExported.
     */
    @SuppressWarnings("unused")
    public void exportDocument(String sourcePath, String suggestedName) {
        runOnUiThread(() -> {
            pendingExportSource = sourcePath;
            Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("application/octet-stream");
            intent.putExtra(Intent.EXTRA_TITLE, suggestedName);
            try {
                startActivityForResult(intent, REQUEST_CREATE_DOCUMENT);
            } catch (ActivityNotFoundException e) {
                Log.e(TAG, "No document picker available", e);
                pendingExportSource = null;
                nativeDocumentExported(false);
            }
        });
    }

    private static String displayName(Uri uri) {
        String path = uri.getLastPathSegment();
        if (path == null) {
            return "import.bin";
        }
        int slash = path.lastIndexOf('/');
        int colon = path.lastIndexOf(':');
        String name = path.substring(Math.max(slash, colon) + 1);
        return name.isEmpty() ? "import.bin" : name.replaceAll("[^A-Za-z0-9._-]", "_");
    }

    private void onDocumentPicked(int resultCode, Intent data) {
        Uri uri = (resultCode == RESULT_OK && data != null) ? data.getData() : null;
        if (uri == null) {
            nativeDocumentPicked(null);
            return;
        }
        new Thread(() -> {
            File dir = new File(dataDirectory(), "imports");
            //noinspection ResultOfMethodCallIgnored
            dir.mkdirs();
            File target = new File(dir, displayName(uri));
            try (InputStream in = getContentResolver().openInputStream(uri);
                 OutputStream out = new FileOutputStream(target)) {
                if (in == null) {
                    throw new IOException("Could not open " + uri);
                }
                copy(in, out);
                nativeDocumentPicked(target.getAbsolutePath());
            } catch (IOException e) {
                Log.e(TAG, "Import failed", e);
                //noinspection ResultOfMethodCallIgnored
                target.delete();
                nativeDocumentPicked(null);
            }
        }, "DocumentImport").start();
    }

    private void onDocumentCreated(int resultCode, Intent data) {
        Uri uri = (resultCode == RESULT_OK && data != null) ? data.getData() : null;
        String source = pendingExportSource;
        pendingExportSource = null;
        if (uri == null || source == null) {
            nativeDocumentExported(false);
            return;
        }
        new Thread(() -> {
            try (InputStream in = new FileInputStream(source);
                 OutputStream out = getContentResolver().openOutputStream(uri, "wt")) {
                if (out == null) {
                    throw new IOException("Could not write " + uri);
                }
                copy(in, out);
                nativeDocumentExported(true);
            } catch (IOException e) {
                Log.e(TAG, "Export failed", e);
                nativeDocumentExported(false);
            }
        }, "DocumentExport").start();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == REQUEST_OPEN_DOCUMENT) {
            onDocumentPicked(resultCode, data);
            return;
        }
        if (requestCode == REQUEST_CREATE_DOCUMENT) {
            onDocumentCreated(resultCode, data);
            return;
        }
        if (requestCode != REQUEST_ROM) {
            super.onActivityResult(requestCode, resultCode, data);
            return;
        }
        Uri uri = (resultCode == RESULT_OK && data != null) ? data.getData() : null;
        if (uri == null) {
            nativeRomImportFinished(null);
            return;
        }

        // Copy off the UI thread: a Platinum ROM is 128 MB
        new Thread(() -> {
            File target = new File(dataDirectory(), "import.nds");
            try (InputStream in = getContentResolver().openInputStream(uri);
                 OutputStream out = new FileOutputStream(target)) {
                if (in == null) {
                    throw new IOException("Could not open " + uri);
                }
                copy(in, out);
                nativeRomImportFinished(target.getAbsolutePath());
            } catch (IOException e) {
                Log.e(TAG, "ROM import failed", e);
                //noinspection ResultOfMethodCallIgnored
                target.delete();
                nativeRomImportFinished(null);
            }
        }, "RomImport").start();
    }

    private static void copy(InputStream in, OutputStream out) throws IOException {
        byte[] buffer = new byte[1 << 20];
        int count;
        while ((count = in.read(buffer)) > 0) {
            out.write(buffer, 0, count);
        }
    }

    /** Implemented in libntr (sim_android.cpp). A null path means the player cancelled. */
    static native void nativeRomImportFinished(String path);

    /** Implemented in libntr (sim_files.cpp). A null path means the player cancelled. */
    static native void nativeDocumentPicked(String path);

    /** Implemented in libntr (sim_files.cpp). */
    static native void nativeDocumentExported(boolean ok);
}
