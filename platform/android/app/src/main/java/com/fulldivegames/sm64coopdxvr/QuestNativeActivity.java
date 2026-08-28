package com.fulldivegames.sm64coopdxvr;

import android.app.NativeActivity;
import android.Manifest;
import android.content.ComponentName;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.PackageInstaller;
import android.app.PendingIntent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Build;
import android.os.Environment;
import android.provider.Settings;
import android.speech.RecognitionListener;
import android.speech.ModelDownloadListener;
import android.speech.RecognizerIntent;
import android.speech.SpeechRecognizer;
import android.util.Log;
import android.widget.Toast;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.Locale;

import org.json.JSONArray;
import org.json.JSONObject;


public final class QuestNativeActivity extends NativeActivity {
    // NativeActivity loads this library for the game loop, but that native
    // load is not associated with this Java class loader on every Quest OS
    // version. Load the same library here as well so JNI speech callbacks can
    // be resolved from Java. Android treats the second load as idempotent.
    static {
        System.loadLibrary("sm64coopdxvr");
    }

    private static final String TAG = "SM64CoopDXVR";
    private static final int OPEN_ROM_REQUEST = 6401;
    private static final int MICROPHONE_PERMISSION_REQUEST = 6402;
    private static final String ROM_NAME = "baserom.us.z64";
    private static final String SHARED_MOD_DIRECTORY = "/sdcard/SM64VR/mods";
    private static final String SHARED_DYNOS_PACK_DIRECTORY =
            "/sdcard/SM64VR/dynos/packs";
    private static final String SHARED_PALETTE_DIRECTORY =
            "/sdcard/SM64VR/palettes";
    private static final String SHARED_SHADER_CACHE_DIRECTORY =
            "/sdcard/SM64VR/shader-cache";
    private static final String SHADER_WARMUP_PENDING_FILE =
            "first-boot-after-rom.pending";
    private boolean requestedSharedStorageAccess;
    private SpeechRecognizer speechRecognizer;
    private boolean speechListening;
    private boolean speechModelDownloadRequested;
    private boolean speechStartPendingAfterModelDownload;

    private static native void nativeOnSpeechRecognitionResult(String text);
    private static native void nativeOnSpeechRecognitionState(boolean listening);
    private static final String US_ROM_SHA1 =
            "9bef1128717f958171a4afac3ed78ee2bb4e86ce";
    private static final String RELEASES_API =
            "https://api.github.com/repos/fulldivegames/sm64coopdx-VR-Standalone-Physics-based/releases?per_page=20";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        installBundledResources();
        normalizeCustomPaletteNames();
        super.onCreate(savedInstanceState);
        if (Build.VERSION.SDK_INT >= 23 &&
                checkSelfPermission(Manifest.permission.RECORD_AUDIO) !=
                    PackageManager.PERMISSION_GRANTED) {
            requestPermissions(
                    new String[] { Manifest.permission.RECORD_AUDIO },
                    MICROPHONE_PERMISSION_REQUEST);
        }
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
            createSharedDirectory(SHARED_PALETTE_DIRECTORY, "palette");
            createSharedDirectory(SHARED_SHADER_CACHE_DIRECTORY, "shader cache");
            return;
        }
        if (!requestedSharedStorageAccess) {
            requestedSharedStorageAccess = true;
            requestSharedStorageAccess();
        }
    }
public void downloadLatestStandaloneUpdateFromNative() {
        Thread updater = new Thread(() -> {
            try {
                String assetUrl = findLatestStandaloneApk();
                if (assetUrl == null) {
                    showUpdateToast("No Quest APK was found in the latest release.");
                    return;
                }
                installApkFromUrl(assetUrl);
            } catch (Exception exception) {
                Log.e(TAG, "Standalone update download failed", exception);
                showUpdateToast("Update download failed. Check your connection and try again.");
            }
        }, "SM64VR-StandaloneUpdater");
        updater.setDaemon(true);
        updater.start();
    }

    private String findLatestStandaloneApk() throws Exception {
        HttpURLConnection connection = null;
        try {
            connection = (HttpURLConnection) new URL(RELEASES_API).openConnection();
            connection.setConnectTimeout(5000);
            connection.setReadTimeout(5000);
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
            for (int releaseIndex = 0; releaseIndex < releases.length(); ++releaseIndex) {
                JSONObject release = releases.getJSONObject(releaseIndex);
                if (release.optBoolean("draft", false) || release.optBoolean("prerelease", false)) continue;
                JSONArray assets = release.optJSONArray("assets");
                if (assets == null) continue;
                String fallback = null;
                for (int assetIndex = 0; assetIndex < assets.length(); ++assetIndex) {
                    JSONObject asset = assets.getJSONObject(assetIndex);
                    String name = asset.optString("name", "").toLowerCase(Locale.ROOT);
                    String url = asset.optString("browser_download_url", "");
                    if (!name.endsWith(".apk") || url.isEmpty()) continue;
                    if (name.contains("quest") || name.contains("standalone") || name.contains("android")) return url;
                    if (fallback == null) fallback = url;
                }
                if (fallback != null) return fallback;
            }
            return null;
        } finally {
            if (connection != null) connection.disconnect();
        }
    }

    private void installApkFromUrl(String assetUrl) throws Exception {
        if (Build.VERSION.SDK_INT >= 26 && !getPackageManager().canRequestPackageInstalls()) {
            Intent settings = new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                    Uri.parse("package:" + getPackageName()));
            settings.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            startActivity(settings);
            showUpdateToast("Allow installs for SM64 Co-Op DX VR, then select Update again.");
            return;
        }
        PackageInstaller installer = getPackageManager().getPackageInstaller();
        PackageInstaller.SessionParams parameters = new PackageInstaller.SessionParams(
                PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        parameters.setAppPackageName(getPackageName());
        int sessionId = installer.createSession(parameters);
        try (PackageInstaller.Session session = installer.openSession(sessionId)) {
            HttpURLConnection connection = (HttpURLConnection) new URL(assetUrl).openConnection();
            connection.setConnectTimeout(10000);
            connection.setReadTimeout(30000);
            connection.setInstanceFollowRedirects(true);
            connection.setRequestProperty("User-Agent", "SM64-Co-Op-DX-VR-Standalone");
            if (connection.getResponseCode() != HttpURLConnection.HTTP_OK) {
                connection.disconnect();
                throw new IOException("APK download returned " + connection.getResponseCode());
            }
            long size = connection.getContentLengthLong();
            try (InputStream input = connection.getInputStream();
                 OutputStream output = session.openWrite("update.apk", 0, size)) {
                byte[] buffer = new byte[64 * 1024];
                int count;
                while ((count = input.read(buffer)) >= 0) {
                    if (count > 0) output.write(buffer, 0, count);
                }
                session.fsync(output);
            } finally {
                connection.disconnect();
            }
            int flags = PendingIntent.FLAG_UPDATE_CURRENT;
            if (Build.VERSION.SDK_INT >= 31) flags |= PendingIntent.FLAG_MUTABLE;
            PendingIntent pending = PendingIntent.getBroadcast(this, sessionId,
                    new Intent(this, UpdateInstallReceiver.class), flags);
            showUpdateToast("Download complete. Android will ask to confirm the update.");
            session.commit(pending.getIntentSender());
        }
    }

    private void showUpdateToast(String message) {
        runOnUiThread(() -> Toast.makeText(this, message, Toast.LENGTH_LONG).show());
    }


    public void startSpeechRecognitionFromNative() {
        runOnUiThread(this::toggleSpeechRecognition);
    }

    private void toggleSpeechRecognition() {
        if (speechListening && speechRecognizer != null) {
            speechRecognizer.cancel();
            speechListening = false;
            nativeOnSpeechRecognitionState(false);
            return;
        }
        if (Build.VERSION.SDK_INT >= 23 &&
                checkSelfPermission(Manifest.permission.RECORD_AUDIO) !=
                    PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] { Manifest.permission.RECORD_AUDIO },
                    MICROPHONE_PERMISSION_REQUEST);
            nativeOnSpeechRecognitionState(false);
            return;
        }
        if (speechRecognizer == null) {
            try {
                if (Build.VERSION.SDK_INT >= 31 &&
                        SpeechRecognizer.isOnDeviceRecognitionAvailable(this)) {
                    speechRecognizer = SpeechRecognizer.createOnDeviceSpeechRecognizer(this);
                } else if (SpeechRecognizer.isRecognitionAvailable(this)) {
                    speechRecognizer = SpeechRecognizer.createSpeechRecognizer(this);
                } else {
                    ComponentName questService = ComponentName.unflattenFromString(
                        "com.oculus.systemintelligence/" +
                        ".android.speech.OnDeviceRecognitionService");
                    speechRecognizer = SpeechRecognizer.createSpeechRecognizer(
                        this, questService);
                }
            } catch (RuntimeException exception) {
                Log.e(TAG, "Could not create speech recognizer", exception);
                nativeOnSpeechRecognitionState(false);
                Toast.makeText(this, "Dictation is unavailable on this headset.",
                        Toast.LENGTH_SHORT).show();
                return;
            }
            speechRecognizer.setRecognitionListener(new RecognitionListener() {
                @Override public void onReadyForSpeech(Bundle params) {
                    speechListening = true;
                    nativeOnSpeechRecognitionState(true);
                }
                @Override public void onBeginningOfSpeech() { }
                @Override public void onRmsChanged(float rmsdB) { }
                @Override public void onBufferReceived(byte[] buffer) { }
                @Override public void onEndOfSpeech() { }
                @Override public void onError(int error) {
                    speechListening = false;
                    nativeOnSpeechRecognitionState(false);
                    Log.w(TAG, "Dictation failed with speech error " + error);
                    if (error == SpeechRecognizer.ERROR_LANGUAGE_UNAVAILABLE) {
                        requestSpeechModelDownload();
                    }
                }
                @Override public void onResults(Bundle results) {
                    speechListening = false;
                    ArrayList<String> matches = results.getStringArrayList(
                            SpeechRecognizer.RESULTS_RECOGNITION);
                    if (matches != null && !matches.isEmpty()) {
                        nativeOnSpeechRecognitionResult(matches.get(0));
                    }
                    nativeOnSpeechRecognitionState(false);
                }
                @Override public void onPartialResults(Bundle partialResults) { }
                @Override public void onEvent(int eventType, Bundle params) { }
            });
        }
        startSpeechRecognitionNow();
    }

    private void startSpeechRecognitionNow() {
        if (speechRecognizer == null) return;
        Intent intent = createSpeechRecognitionIntent();
        try {
            speechListening = true;
            nativeOnSpeechRecognitionState(true);
            speechRecognizer.startListening(intent);
        } catch (RuntimeException exception) {
            speechListening = false;
            nativeOnSpeechRecognitionState(false);
            Log.e(TAG, "Could not start dictation", exception);
        }
    }

    private Intent createSpeechRecognitionIntent() {
        Intent intent = new Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH);
        intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL,
                RecognizerIntent.LANGUAGE_MODEL_FREE_FORM);
        intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE,
                Locale.getDefault().toLanguageTag());
        intent.putExtra(RecognizerIntent.EXTRA_MAX_RESULTS, 1);
        intent.putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, false);
        intent.putExtra(RecognizerIntent.EXTRA_PREFER_OFFLINE, true);
        return intent;
    }

    private void requestSpeechModelDownload() {
        if (Build.VERSION.SDK_INT < 33 || speechRecognizer == null) {
            Toast.makeText(this,
                    "Quest dictation needs an English speech model that is not installed.",
                    Toast.LENGTH_LONG).show();
            return;
        }
        speechStartPendingAfterModelDownload = true;
        if (speechModelDownloadRequested) {
            Toast.makeText(this,
                    "Quest is still preparing the English dictation model.",
                    Toast.LENGTH_LONG).show();
            return;
        }

        speechModelDownloadRequested = true;
        Intent intent = createSpeechRecognitionIntent();
        try {
            if (Build.VERSION.SDK_INT >= 34) {
                speechRecognizer.triggerModelDownload(intent, getMainExecutor(),
                        new ModelDownloadListener() {
                            @Override public void onProgress(int completedPercent) { }

                            @Override public void onSuccess() {
                                speechModelDownloadRequested = false;
                                if (speechStartPendingAfterModelDownload) {
                                    speechStartPendingAfterModelDownload = false;
                                    Toast.makeText(QuestNativeActivity.this,
                                            "Dictation is ready. Listening now.",
                                            Toast.LENGTH_SHORT).show();
                                    startSpeechRecognitionNow();
                                }
                            }

                            @Override public void onScheduled() {
                                Toast.makeText(QuestNativeActivity.this,
                                        "Quest scheduled the English dictation model download.",
                                        Toast.LENGTH_LONG).show();
                            }

                            @Override public void onError(int error) {
                                speechModelDownloadRequested = false;
                                speechStartPendingAfterModelDownload = false;
                                Log.w(TAG, "Speech model download failed with error " + error);
                                Toast.makeText(QuestNativeActivity.this,
                                        "Quest could not download its dictation model.",
                                        Toast.LENGTH_LONG).show();
                            }
                        });
            } else {
                speechRecognizer.triggerModelDownload(intent);
                Toast.makeText(this,
                        "Quest requested the English dictation model. Try Mic again shortly.",
                        Toast.LENGTH_LONG).show();
            }
        } catch (RuntimeException exception) {
            speechModelDownloadRequested = false;
            speechStartPendingAfterModelDownload = false;
            Log.e(TAG, "Could not request speech model download", exception);
            Toast.makeText(this,
                    "Quest could not request its dictation model.",
                    Toast.LENGTH_LONG).show();
        }
    }

    @Override
    protected void onDestroy() {
        if (speechRecognizer != null) {
            speechRecognizer.destroy();
            speechRecognizer = null;
        }
        speechListening = false;
        nativeOnSpeechRecognitionState(false);
        super.onDestroy();
    }

    private void prepareSharedContentDirectories() {
        if (Build.VERSION.SDK_INT < 30 || Environment.isExternalStorageManager()) {
            createSharedDirectory(SHARED_MOD_DIRECTORY, "mod");
            createSharedDirectory(SHARED_DYNOS_PACK_DIRECTORY, "DynOS pack");
            createSharedDirectory(SHARED_PALETTE_DIRECTORY, "palette");
            createSharedDirectory(SHARED_SHADER_CACHE_DIRECTORY, "shader cache");
        }
    }

    private void normalizeCustomPaletteNames() {
        File paletteDirectory = new File(getExternalFilesDir(null), "palettes");
        File[] palettes = paletteDirectory.listFiles(file ->
                file.isFile() && file.getName().toLowerCase(Locale.ROOT).endsWith(".ini") &&
                !isBundledCharacterPalette(file.getName()));
        if (palettes == null || palettes.length == 0) return;

        boolean alreadyNormalized = palettes.length <= 100;
        for (int number = 1; alreadyNormalized && number <= palettes.length; number++) {
            File expected = new File(paletteDirectory, "Custom " + number + ".ini");
            alreadyNormalized = expected.isFile();
        }
        if (alreadyNormalized) return;

        Arrays.sort(palettes, Comparator
                .comparingLong(File::lastModified)
                .thenComparing(File::getName, String.CASE_INSENSITIVE_ORDER));
        int count = Math.min(palettes.length, 100);
        File[] temporary = new File[count];
        for (int i = 0; i < count; i++) {
            temporary[i] = new File(paletteDirectory,
                    ".palette-migration-" + i + ".tmp");
            if (!palettes[i].equals(temporary[i]) &&
                    !palettes[i].renameTo(temporary[i])) {
                Log.w(TAG, "Could not stage palette rename: " + palettes[i]);
                temporary[i] = null;
            }
        }
        for (int i = 0; i < count; i++) {
            if (temporary[i] == null) continue;
            File destination = new File(paletteDirectory,
                    "Custom " + (i + 1) + ".ini");
            if (!temporary[i].renameTo(destination)) {
                Log.w(TAG, "Could not rename palette to " + destination);
            }
        }
    }

    private boolean isBundledCharacterPalette(String fileName) {
        return fileName.equalsIgnoreCase("Mario.ini") ||
                fileName.equalsIgnoreCase("Luigi.ini") ||
                fileName.equalsIgnoreCase("Toad.ini") ||
                fileName.equalsIgnoreCase("Wario.ini") ||
                fileName.equalsIgnoreCase("Waluigi.ini");
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
                        "File access is required for /sdcard/SM64VR mods, DynOS packs, palettes, and shader cache.",
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
            copyAssetFile("release_notes.txt", new File(root, "release_notes.txt"));
            Log.i(TAG, "Release notes installed.");
            copyMissingAssetDirectory("palettes", new File(root, "palettes"));
            Log.i(TAG, "Bundled character palettes installed.");
            // Remove obsolete synchronized/session manifests. Native Fire
            // Flowers are deliberately not advertised to public clients.
            for (String obsoleteMod :
                    new String[] { "Fire Flowers", "vr-special-moves" }) {
                deleteBundledDirectory(
                        new File(SHARED_MOD_DIRECTORY, obsoleteMod));
                deleteBundledDirectory(
                        new File(root, "mods/" + obsoleteMod));
            }
            copyMissingAssetDirectory("mods", new File(root, "mods"));
            Log.i(TAG, "Bundled session mods installed.");
            copyAssetDirectory("sonic_shoes", new File(root, "sonic_shoes"));
            Log.i(TAG, "Bundled Sonic Shoes music installed.");
        } catch (IOException exception) {
            Log.e(TAG, "Could not install bundled resources.", exception);
        }
    }

    private void deleteBundledDirectory(File file) throws IOException {
        if (!file.exists()) return;
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) {
                for (File child : children) deleteBundledDirectory(child);
            }
        }
        if (!file.delete()) {
            throw new IOException("Could not remove obsolete bundled resource " + file);
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
            markShaderWarmupPending();
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

    private void markShaderWarmupPending() {
        File cacheDirectory = new File(SHARED_SHADER_CACHE_DIRECTORY);
        if (!cacheDirectory.isDirectory() && !cacheDirectory.mkdirs()) {
            Log.w(TAG, "Could not create shader cache directory after ROM import.");
            return;
        }
        File pending = new File(cacheDirectory, SHADER_WARMUP_PENDING_FILE);
        try (FileOutputStream output = new FileOutputStream(pending, false)) {
            output.write("pending\n".getBytes("UTF-8"));
            output.flush();
            Log.i(TAG, "Next launch will complete the post-ROM shader warmup.");
        } catch (IOException exception) {
            Log.w(TAG, "Could not mark post-ROM shader warmup as pending.", exception);
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
