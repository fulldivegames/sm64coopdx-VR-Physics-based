package com.fulldivegames.sm64coopdxvr;

import android.app.NativeActivity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Build;
import android.os.Environment;
import android.provider.Settings;
import android.util.Log;
import android.graphics.Color;
import android.text.Editable;
import android.text.InputType;
import android.text.TextWatcher;
import android.view.Gravity;
import android.view.WindowManager;
import android.view.inputmethod.InputMethodManager;
import android.content.Context;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.Toast;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.IOException;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;
import java.util.Locale;

import org.json.JSONArray;


public final class QuestNativeActivity extends NativeActivity {
    private static final String TAG = "SM64CoopDXVR";
    private static final int OPEN_ROM_REQUEST = 6401;
    private static final String ROM_NAME = "baserom.us.z64";
    private static final String SHARED_MOD_DIRECTORY = "/sdcard/SM64VR/mods";
    private static final String SHARED_DYNOS_PACK_DIRECTORY =
            "/sdcard/SM64VR/dynos/packs";
    private boolean requestedSharedStorageAccess;
    private EditText vrKeyboardInput;
    private boolean suppressVrKeyboardText;
    private static final String US_ROM_SHA1 =
            "9bef1128717f958171a4afac3ed78ee2bb4e86ce";
    private static final String RELEASES_API =
            "https://api.github.com/repos/fulldivegames/sm64coopdx-VR-Standalone/releases?per_page=20";

    private static native void nativeOnVrKeyboardText(String text);

    public void showVrKeyboard(final String initialText) {
        runOnUiThread(() -> {
            if (vrKeyboardInput == null) {
                vrKeyboardInput = new EditText(this);
                vrKeyboardInput.setSingleLine(true);
                vrKeyboardInput.setInputType(InputType.TYPE_CLASS_TEXT |
                        InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
                vrKeyboardInput.setTextColor(Color.TRANSPARENT);
                vrKeyboardInput.setBackgroundColor(Color.TRANSPARENT);
                vrKeyboardInput.setAlpha(0.01f);
                vrKeyboardInput.addTextChangedListener(new TextWatcher() {
                    @Override public void beforeTextChanged(CharSequence s,
                            int start, int count, int after) {}
                    @Override public void onTextChanged(CharSequence s,
                            int start, int before, int count) {}
                    @Override public void afterTextChanged(Editable editable) {
                        if (!suppressVrKeyboardText) {
                            nativeOnVrKeyboardText(editable.toString());
                        }
                    }
                });
                FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
                        32, 32, Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL);
                addContentView(vrKeyboardInput, params);
            }
            suppressVrKeyboardText = true;
            vrKeyboardInput.setText(initialText != null ? initialText : "");
            vrKeyboardInput.setSelection(vrKeyboardInput.length());
            suppressVrKeyboardText = false;
            vrKeyboardInput.setVisibility(EditText.VISIBLE);
            vrKeyboardInput.requestFocusFromTouch();
            getWindow().setSoftInputMode(
                    WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING |
                    WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE);
            vrKeyboardInput.postDelayed(() -> {
                InputMethodManager inputMethod = (InputMethodManager)
                        getSystemService(Context.INPUT_METHOD_SERVICE);
                if (inputMethod != null && vrKeyboardInput.hasFocus()) {
                    inputMethod.restartInput(vrKeyboardInput);
                    inputMethod.showSoftInput(vrKeyboardInput,
                            InputMethodManager.SHOW_FORCED);
                }
            }, 150);
        });
    }

    public void hideVrKeyboard() {
        runOnUiThread(() -> {
            if (vrKeyboardInput == null) return;
            InputMethodManager inputMethod = (InputMethodManager)
                    getSystemService(Context.INPUT_METHOD_SERVICE);
            if (inputMethod != null) {
                inputMethod.hideSoftInputFromWindow(
                        vrKeyboardInput.getWindowToken(), 0);
            }
            vrKeyboardInput.clearFocus();
            vrKeyboardInput.setVisibility(EditText.GONE);
        });
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        installBundledResources();
        super.onCreate(savedInstanceState);
        prepareSharedContentDirectories();
        checkForStandaloneUpdate();
        File rom = new File(getExternalFilesDir(null), ROM_NAME);
        if (!rom.isFile() && savedInstanceState == null) {
            openRomPicker();
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (Build.VERSION.SDK_INT < 30 || Environment.isExternalStorageManager()) {
            createSharedDirectory(SHARED_MOD_DIRECTORY, "mod");
            createSharedDirectory(SHARED_DYNOS_PACK_DIRECTORY, "DynOS pack");
            return;
        }
        if (!requestedSharedStorageAccess) {
            requestedSharedStorageAccess = true;
            requestSharedStorageAccess();
        }
    }

    private void prepareSharedContentDirectories() {
        if (Build.VERSION.SDK_INT < 30 || Environment.isExternalStorageManager()) {
            createSharedDirectory(SHARED_MOD_DIRECTORY, "mod");
            createSharedDirectory(SHARED_DYNOS_PACK_DIRECTORY, "DynOS pack");
        }
    }

    private void createSharedDirectory(String path, String label) {
        File directory = new File(path);
        if (!directory.isDirectory() && !directory.mkdirs()) {
            Log.w(TAG, "Could not create shared " + label + " directory: " + directory);
        }
    }

    private void requestSharedStorageAccess() {
        try {
            Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
            intent.setData(Uri.parse("package:" + getPackageName()));
            startActivity(intent);
        } catch (Exception exception) {
            Log.w(TAG, "App-specific file access settings unavailable.", exception);
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            } catch (Exception fallbackException) {
                Log.e(TAG, "File access settings unavailable.", fallbackException);
                Toast.makeText(this,
                        "File access is required for /sdcard/SM64VR mods and DynOS packs.",
                        Toast.LENGTH_LONG).show();
            }
        }
    }

    private void checkForStandaloneUpdate() {
        Thread checker = new Thread(() -> {
            HttpURLConnection connection = null;
            try {
                connection = (HttpURLConnection) new URL(RELEASES_API).openConnection();
                connection.setConnectTimeout(3000);
                connection.setReadTimeout(3000);
                connection.setRequestProperty("Accept", "application/vnd.github+json");
                connection.setRequestProperty("User-Agent", "SM64-Co-Op-DX-VR-Standalone");
                if (connection.getResponseCode() != HttpURLConnection.HTTP_OK) {
                    throw new IOException("GitHub returned " + connection.getResponseCode());
                }
                StringBuilder json = new StringBuilder();
                byte[] buffer = new byte[8192];
                try (InputStream input = connection.getInputStream()) {
                    int count;
                    while ((count = input.read(buffer)) >= 0 && json.length() < 524288) {
                        if (count > 0) json.append(new String(buffer, 0, count, "UTF-8"));
                    }
                }
                JSONArray releases = new JSONArray(json.toString());
                for (int i = 0; i < releases.length(); ++i) {
                    String tag = releases.getJSONObject(i).optString("tag_name", "");
                    if (tag.startsWith("v")) {
                        saveStandaloneUpdateStatus(true, tag);
                        return;
                    }
                    if (tag.startsWith("android-v")) {
                        saveStandaloneUpdateStatus(true, tag.substring("android-".length()));
                        return;
                    }
                }
                saveStandaloneUpdateStatus(true, "");
            } catch (Exception exception) {
                Log.w(TAG, "Standalone update check unavailable.", exception);
                saveStandaloneUpdateStatus(false, "");
            } finally {
                if (connection != null) connection.disconnect();
            }
        }, "SM64VR-UpdateCheck");
        checker.setDaemon(true);
        checker.start();
    }

    private void saveStandaloneUpdateStatus(boolean succeeded, String version) {
        File root = getExternalFilesDir(null);
        if (root == null) return;
        File temporary = new File(root, "vr-update-status.tmp");
        File destination = new File(root, "vr-update-status.txt");
        String value = (succeeded ? "ok\n" : "failed\n") + version + "\n";
        try (FileOutputStream output = new FileOutputStream(temporary)) {
            output.write(value.getBytes("UTF-8"));
            output.flush();
            if (destination.exists() && !destination.delete()) {
                throw new IOException("Could not replace update status");
            }
            if (!temporary.renameTo(destination)) {
                throw new IOException("Could not finish update status");
            }
        } catch (IOException exception) {
            temporary.delete();
            Log.w(TAG, "Could not save standalone update status.", exception);
        }
    }

    private void installBundledResources() {
        File root = getExternalFilesDir(null);
        if (root == null) {
            Log.e(TAG, "External app data directory is unavailable.");
            return;
        }
        try {
            copyAssetDirectory("lang", new File(root, "lang"));
            Log.i(TAG, "Bundled language resources installed.");
            copyMissingAssetDirectory("palettes", new File(root, "palettes"));
            Log.i(TAG, "Bundled character palettes installed.");
        } catch (IOException exception) {
            Log.e(TAG, "Could not install bundled language resources.", exception);
        }
    }

    private void copyMissingAssetDirectory(String assetPath, File destination)
            throws IOException {
        String[] children = getAssets().list(assetPath);
        if (children == null || children.length == 0) {
            if (!destination.exists()) copyAssetFile(assetPath, destination);
            return;
        }
        if (!destination.isDirectory() && !destination.mkdirs()) {
            throw new IOException("Could not create " + destination);
        }
        for (String child : children) {
            copyMissingAssetDirectory(assetPath + "/" + child,
                    new File(destination, child));
        }
    }

    private void copyAssetDirectory(String assetPath, File destination)
            throws IOException {
        String[] children = getAssets().list(assetPath);
        if (children == null || children.length == 0) {
            copyAssetFile(assetPath, destination);
            return;
        }
        if (!destination.isDirectory() && !destination.mkdirs()) {
            throw new IOException("Could not create " + destination);
        }
        for (String child : children) {
            copyAssetDirectory(assetPath + "/" + child,
                    new File(destination, child));
        }
    }

    private void copyAssetFile(String assetPath, File destination)
            throws IOException {
        File parent = destination.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IOException("Could not create " + parent);
        }
        byte[] buffer = new byte[16 * 1024];
        try (InputStream input = getAssets().open(assetPath);
             FileOutputStream output = new FileOutputStream(destination)) {
            int count;
            while ((count = input.read(buffer)) >= 0) {
                if (count > 0) output.write(buffer, 0, count);
            }
        }
    }

    private void openRomPicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("application/octet-stream");
        intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {
                "application/octet-stream", "application/x-n64-rom", "*/*"
        });
        startActivityForResult(intent, OPEN_ROM_REQUEST);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != OPEN_ROM_REQUEST || resultCode != RESULT_OK
                || data == null || data.getData() == null) {
            return;
        }
        importRom(data.getData());
    }

    private void importRom(Uri uri) {
        File destination = new File(getExternalFilesDir(null), ROM_NAME);
        File temporary = new File(getExternalFilesDir(null), ROM_NAME + ".tmp");
        byte[] buffer = new byte[64 * 1024];
        try (InputStream input = getContentResolver().openInputStream(uri);
             FileOutputStream output = new FileOutputStream(temporary)) {
            if (input == null) throw new IllegalStateException("ROM stream unavailable");
            int count;
            while ((count = input.read(buffer)) >= 0) {
                if (count > 0) output.write(buffer, 0, count);
            }
        } catch (Exception exception) {
            temporary.delete();
            reportImportError("Could not copy the selected ROM.", exception);
            return;
        }

        try {
            String sha1 = sha1(temporary);
            if (!US_ROM_SHA1.equals(sha1)) {
                temporary.delete();
                reportImportError("That is not the supported US SM64 ROM.", null);
                return;
            }
            if (destination.exists() && !destination.delete()) {
                throw new IllegalStateException("Could not replace existing ROM");
            }
            if (!temporary.renameTo(destination)) {
                throw new IllegalStateException("Could not finish ROM import");
            }
            Log.i(TAG, "US SM64 ROM imported successfully from Android file picker.");
            Toast.makeText(this, "SM64 US ROM imported successfully.", Toast.LENGTH_LONG).show();
            // The native OpenXR host checks the ROM once during startup. A
            // first-run picker returns after that check, so recreate the
            // activity to let the native host validate and boot the newly
            // imported ROM without requiring the player to quit manually.
            recreate();
        } catch (Exception exception) {
            temporary.delete();
            reportImportError("Could not validate the selected ROM.", exception);
        }
    }

    private static String sha1(File file) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-1");
        byte[] buffer = new byte[64 * 1024];
        try (FileInputStream input = new FileInputStream(file)) {
            int count;
            while ((count = input.read(buffer)) >= 0) {
                if (count > 0) digest.update(buffer, 0, count);
            }
        }
        StringBuilder value = new StringBuilder(40);
        for (byte item : digest.digest()) {
            value.append(String.format(Locale.ROOT, "%02x", item & 0xff));
        }
        return value.toString();
    }

    private void reportImportError(String message, Exception exception) {
        if (exception == null) Log.e(TAG, message);
        else Log.e(TAG, message, exception);
        Toast.makeText(this, message, Toast.LENGTH_LONG).show();
    }
}
