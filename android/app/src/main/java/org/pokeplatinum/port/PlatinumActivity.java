package org.pokeplatinum.port;

import android.Manifest;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.Settings;
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
    private static final int REQUEST_OPEN_FOLDER = 0x504F;
    private static final int REQUEST_IMPORT_FOLDER = 0x5050;
    private static final int REQUEST_FOLDER_PATH = 0x5051;
    private static final int REQUEST_STORAGE = 0x5052;

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

    /**
     * Called from native code (sim_carts.cpp) after switching to another game: start the
     * app again in a fresh process, which then enters that game's directory.
     */
    @SuppressWarnings("unused")
    public void restartApp() {
        Intent intent = getPackageManager().getLaunchIntentForPackage(getPackageName());
        if (intent != null) {
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
            startActivity(intent);
        }
        Runtime.getRuntime().exit(0);
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

    /** Game code (header bytes 0x0C-0x0F) the ROMs copied from a picked folder must have. */
    private String folderGameCode;

    /**
     * Called from native code (sim_files.cpp): let the player pick a folder, e.g. one with
     * a ROM and patches for it. Its patches (.xdelta, .vcdiff) and the ROMs of this game
     * (gameCode, or every .nds/.srl when empty) are copied, also from one folder level
     * down, into imports/folder/NAME in the data directory, whose path is reported
     * through nativeDocumentPicked. Archives and unsupported patch formats it holds are
     * listed in .skipped there.
     */
    @SuppressWarnings("unused")
    public void pickFolder(String gameCode) {
        folderGameCode = gameCode != null ? gameCode : "";
        runOnUiThread(() -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            try {
                startActivityForResult(intent, REQUEST_OPEN_FOLDER);
            } catch (ActivityNotFoundException e) {
                Log.e(TAG, "No folder picker available", e);
                nativeDocumentPicked(null);
            }
        });
    }

    private static boolean hasExtension(String name, String... extensions) {
        String lower = name.toLowerCase();
        for (String extension : extensions) {
            if (lower.endsWith(extension)) {
                return true;
            }
        }
        return false;
    }

    private static String safeName(String name) {
        String safe = name.replaceAll("[^A-Za-z0-9._ ()-]", "_").trim();
        return safe.isEmpty() || safe.startsWith(".") ? "_" + safe : safe;
    }

    /** The ROM's game code is the expected one (it is read without copying the ROM). */
    private boolean isThisGame(Uri document) {
        if (folderGameCode.isEmpty()) {
            return true;
        }
        try (InputStream in = getContentResolver().openInputStream(document)) {
            byte[] header = new byte[16];
            int read = 0;
            while (in != null && read < header.length) {
                int count = in.read(header, read, header.length - read);
                if (count <= 0) {
                    break;
                }
                read += count;
            }
            return read == header.length
                && new String(header, 12, 4, java.nio.charset.StandardCharsets.US_ASCII).equals(folderGameCode);
        } catch (IOException | RuntimeException e) {
            return false;
        }
    }

    /** Copies the patches and ROMs of a picked folder (and of its direct subfolders). */
    private void copyFolder(Uri tree, String documentId, File target, int depth, StringBuilder skipped)
            throws IOException {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, documentId);
        String[] columns = {
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE,
        };
        try (Cursor cursor = getContentResolver().query(children, columns, null, null, null)) {
            if (cursor == null) {
                throw new IOException("Could not list " + tree);
            }
            while (cursor.moveToNext()) {
                String id = cursor.getString(0);
                String name = cursor.getString(1);
                String mime = cursor.getString(2);
                if (name == null) {
                    continue;
                }
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    if (depth == 0) {
                        copyFolder(tree, id, target, depth + 1, skipped);
                    }
                    continue;
                }
                Uri document = DocumentsContract.buildDocumentUriUsingTree(tree, id);
                boolean patch = hasExtension(name, ".xdelta", ".vcdiff", ".delta");
                boolean rom = hasExtension(name, ".nds", ".srl");
                if (hasExtension(name, ".zip", ".7z", ".rar", ".bps", ".ips", ".ups")) {
                    skipped.append(name).append('\n');
                }
                if (!patch && !(rom && isThisGame(document))) {
                    continue;
                }
                File out = new File(target, safeName(name));
                try (InputStream in = getContentResolver().openInputStream(document);
                     OutputStream os = new FileOutputStream(out)) {
                    if (in == null) {
                        throw new IOException("Could not open " + name);
                    }
                    copy(in, os);
                }
            }
        }
    }

    private String treeName(Uri tree) {
        Uri root = DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree));
        try (Cursor cursor = getContentResolver().query(root,
                new String[] {DocumentsContract.Document.COLUMN_DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst() && cursor.getString(0) != null) {
                return safeName(cursor.getString(0));
            }
        } catch (RuntimeException e) {
            Log.w(TAG, "No name for " + tree, e);
        }
        return "folder";
    }

    private void onFolderPicked(int resultCode, Intent data) {
        Uri tree = (resultCode == RESULT_OK && data != null) ? data.getData() : null;
        if (tree == null) {
            nativeDocumentPicked(null);
            return;
        }
        new Thread(() -> {
            File folders = new File(new File(dataDirectory(), "imports"), "folder");
            deleteRecursively(folders);
            File target = new File(folders, treeName(tree));
            //noinspection ResultOfMethodCallIgnored
            target.mkdirs();
            try {
                StringBuilder skipped = new StringBuilder();
                copyFolder(tree, DocumentsContract.getTreeDocumentId(tree), target, 0, skipped);
                if (skipped.length() > 0) {
                    try (OutputStream os = new FileOutputStream(new File(target, ".skipped"))) {
                        os.write(skipped.toString().getBytes(java.nio.charset.StandardCharsets.UTF_8));
                    }
                }
                nativeDocumentPicked(target.getAbsolutePath());
            } catch (IOException | RuntimeException e) {
                Log.e(TAG, "Folder import failed", e);
                deleteRecursively(folders);
                nativeDocumentPicked(null);
            }
        }, "FolderImport").start();
    }

    /** Extensions (".png,.dds") and target directory of the folder import in progress. */
    private String importExtensions = "";
    private String importTarget = "";

    /**
     * Called from native code (sim_files.cpp): let the player pick a folder whose files with
     * one of the extensions are copied, keeping its folders, into target/NAME (NAME: the
     * folder's). That path is reported through nativeDocumentPicked (null if cancelled or
     * nothing was copied). A texture pack, say.
     */
    @SuppressWarnings("unused")
    public void importFolder(String extensions, String target) {
        importExtensions = ("," + (extensions != null ? extensions : "") + ",").toLowerCase();
        importTarget = target != null ? target : "";
        runOnUiThread(() -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            try {
                startActivityForResult(intent, REQUEST_IMPORT_FOLDER);
            } catch (ActivityNotFoundException e) {
                Log.e(TAG, "No folder picker available", e);
                nativeDocumentPicked(null);
            }
        });
    }

    private boolean wantedForImport(String name) {
        int dot = name.lastIndexOf('.');
        return dot > 0 && importExtensions.contains("," + name.substring(dot).toLowerCase() + ",");
    }

    /** Copies a picked folder's wanted files and those of every folder in it. */
    private int importTree(Uri tree, String documentId, File target, int depth) throws IOException {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, documentId);
        String[] columns = {
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE,
        };
        int copied = 0;
        try (Cursor cursor = getContentResolver().query(children, columns, null, null, null)) {
            if (cursor == null) {
                throw new IOException("Could not list " + tree);
            }
            while (cursor.moveToNext()) {
                String id = cursor.getString(0);
                String name = cursor.getString(1);
                String mime = cursor.getString(2);
                if (name == null) {
                    continue;
                }
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    if (depth < 16) {
                        copied += importTree(tree, id, new File(target, safeName(name)), depth + 1);
                    }
                    continue;
                }
                if (!wantedForImport(name)) {
                    continue;
                }
                //noinspection ResultOfMethodCallIgnored
                target.mkdirs();
                Uri document = DocumentsContract.buildDocumentUriUsingTree(tree, id);
                try (InputStream in = getContentResolver().openInputStream(document);
                     OutputStream os = new FileOutputStream(new File(target, safeName(name)))) {
                    if (in == null) {
                        throw new IOException("Could not open " + name);
                    }
                    copy(in, os);
                }
                copied++;
            }
        }
        return copied;
    }

    private void onImportFolderPicked(int resultCode, Intent data) {
        Uri tree = (resultCode == RESULT_OK && data != null) ? data.getData() : null;
        if (tree == null || importTarget.isEmpty()) {
            nativeDocumentPicked(null);
            return;
        }
        new Thread(() -> {
            File target = new File(importTarget, treeName(tree));
            try {
                int copied = importTree(tree, DocumentsContract.getTreeDocumentId(tree), target, 0);
                nativeDocumentPicked(copied > 0 ? target.getAbsolutePath() : null);
            } catch (IOException | RuntimeException e) {
                Log.e(TAG, "Folder import failed", e);
                nativeDocumentPicked(null);
            }
        }, "FolderImport").start();
    }

    /**
     * Called from native code (sim_files.cpp): let the player pick a folder to use where it
     * is, e.g. one Syncthing keeps in step with a computer (save sync). Its path is reported
     * through nativeDocumentPicked: null if cancelled or not on the device's storage. Reading
     * and writing it takes "All files access" (hasAllFilesAccess).
     */
    @SuppressWarnings("unused")
    public void pickFolderPath() {
        runOnUiThread(() -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            try {
                startActivityForResult(intent, REQUEST_FOLDER_PATH);
            } catch (ActivityNotFoundException e) {
                Log.e(TAG, "No folder picker available", e);
                nativeDocumentPicked(null);
            }
        });
    }

    private void onFolderPathPicked(int resultCode, Intent data) {
        Uri tree = (resultCode == RESULT_OK && data != null) ? data.getData() : null;
        String path = treeToPath(tree);
        if (tree != null && path == null) {
            Log.w(TAG, "Not a folder on the device's storage: " + tree);
        }
        nativeDocumentPicked(path);
    }

    /** The file path of a folder the system picker chose on the device's storage, else null. */
    private static String treeToPath(Uri tree) {
        if (tree == null || !"com.android.externalstorage.documents".equals(tree.getAuthority())) {
            return null;
        }
        String id;
        try {
            id = DocumentsContract.getTreeDocumentId(tree);
        } catch (IllegalArgumentException e) {
            return null;
        }
        int colon = id.indexOf(':');
        String volume = colon >= 0 ? id.substring(0, colon) : id;
        String relative = colon >= 0 ? id.substring(colon + 1) : "";
        String base;
        if ("primary".equalsIgnoreCase(volume)) {
            base = Environment.getExternalStorageDirectory().getAbsolutePath();
        } else if ("home".equalsIgnoreCase(volume)) {
            base = Environment.getExternalStorageDirectory().getAbsolutePath() + "/Documents";
        } else {
            base = "/storage/" + volume; // an SD card or USB drive
        }
        return relative.isEmpty() ? base : base + "/" + relative;
    }

    /** Called from native code: whether files anywhere on the device's storage can be used. */
    @SuppressWarnings("unused")
    public boolean hasAllFilesAccess() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            return Environment.isExternalStorageManager();
        }
        return checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
    }

    /**
     * Called from native code: asks for that. On Android 11 and later that's a settings
     * screen ("Allow access to manage all files") the player turns on and comes back from.
     */
    @SuppressWarnings("unused")
    public void requestAllFilesAccess() {
        runOnUiThread(() -> {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                try {
                    startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                            Uri.parse("package:" + getPackageName())));
                } catch (ActivityNotFoundException e) {
                    try {
                        startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                    } catch (ActivityNotFoundException e2) {
                        Log.e(TAG, "No all-files-access settings screen", e2);
                    }
                }
            } else {
                requestPermissions(new String[] { Manifest.permission.READ_EXTERNAL_STORAGE,
                        Manifest.permission.WRITE_EXTERNAL_STORAGE }, REQUEST_STORAGE);
            }
        });
    }

    private static void deleteRecursively(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                deleteRecursively(child);
            }
        }
        //noinspection ResultOfMethodCallIgnored
        file.delete();
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
        if (requestCode == REQUEST_OPEN_FOLDER) {
            onFolderPicked(resultCode, data);
            return;
        }
        if (requestCode == REQUEST_IMPORT_FOLDER) {
            onImportFolderPicked(resultCode, data);
            return;
        }
        if (requestCode == REQUEST_FOLDER_PATH) {
            onFolderPathPicked(resultCode, data);
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
