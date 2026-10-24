package com.fulldivegames.sm64coopdxvr;

import android.app.NativeActivity;
import android.app.AlertDialog;
import android.Manifest;
import android.content.ActivityNotFoundException;
import android.content.ComponentName;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.PackageInstaller;
import android.app.PendingIntent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Build;
import android.os.Environment;
import android.os.Handler;
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


public final class QuestNativeActivity extends NativeActivity implements SteamFrameRomBrowser.Presenter {
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
    private static final int INSTALL_PERMISSION_REQUEST = 6403;
    private static final int STEAM_FRAME_STORAGE_PERMISSION_REQUEST = 6404;
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
    private boolean storageAccessPromptActive;
    private boolean storageAccessPromptCompleted;
    private boolean microphonePermissionPending;
    private boolean romPickerRequested;
    private boolean romPickerLaunchPending;
    private boolean romPickerAwaitingResult;
    private boolean romPickerFallbackScheduled;
    private boolean romPickerFallbackDialogVisible;
    private boolean steamFrameContentPickerAttempted;
    private SteamFrameRomBrowser steamFrameRomBrowser;
    private boolean steamFrameBrowserAfterStorageRequest;
    private boolean steamFrameVoicePermissionRequested;
    private boolean steamFrameStartupErrorShown;
    private boolean steamFrameAudioErrorShown;
    private volatile File steamFrameDataRoot;
    private boolean steamFrameResourcesReady;
    private boolean steamFrameImportPending;
    private AlertDialog steamFramePreparation;
    private SteamFrameSetupPanel steamFrameSetupPanel;
    private boolean steamFrameResumed;
    private final SteamFrameStartupState steamFrameStartup = new SteamFrameStartupState();
    private boolean standaloneUpdateCheckStarted;
    private boolean firstLaunchSetupComplete;
    private volatile String pendingStandaloneUpdateUrl;
    private volatile boolean standaloneInstallPermissionPromptActive;
    private volatile boolean standaloneUpdateInProgress;
    private boolean standalonePermissionRetryScheduled;
    private SpeechRecognizer speechRecognizer;
    private boolean speechListening;
    private boolean speechModelDownloadRequested;
    private boolean speechStartPendingAfterModelDownload;

    private static native void nativeOnSpeechRecognitionResult(String text);
    private static native void nativeOnSpeechRecognitionState(boolean listening);
    private static native void nativeOnRomImported(String dataRoot, int attempt);
    private static native void nativeConfirmRomReady(int attempt);
    private static native void nativeCancelSteamFrameStartup();
    private static native void nativeRetryAudio();
    private static native void nativeSetupPixels(int[] pixels, int generation);

    @Override public void showSteamFrameMenu(String title, String[] names, Runnable[] actions) {
        if (!BuildConfig.STEAM_FRAME || isFinishing() || isDestroyed()) return;
        if (steamFrameSetupPanel == null) steamFrameSetupPanel = new SteamFrameSetupPanel(QuestNativeActivity::nativeSetupPixels);
        steamFrameSetupPanel.show(title, names, actions);
    }

    public void onSteamFrameSetupInput(int direction, boolean select, int generation) {
        runOnUiThread(() -> {
            if (!isFinishing() && !isDestroyed() && steamFrameSetupPanel != null)
                steamFrameSetupPanel.input(direction, select, generation);
        });
    }
    private static final String US_ROM_SHA1 =
            "9bef1128717f958171a4afac3ed78ee2bb4e86ce";
    private static final String RELEASES_API =
            "https://api.github.com/repos/fulldivegames/sm64coopdx-VR-Standalone-Physics-based/releases?per_page=20";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        if (BuildConfig.STEAM_FRAME) {
            super.onCreate(savedInstanceState);
            showSteamFrameMenu("Preparing game files — please wait",
                    new String[] { "Close game" }, new Runnable[] { this::finish });
            // Copying bundled assets must not block Android's UI/lifecycle thread.
            new Thread(() -> {
                try {
                    steamFrameDataRoot = SteamFrameStorage.select(this);
                    installBundledResources();
                    normalizeCustomPaletteNames();
                    runOnUiThread(() -> {
                        if (isFinishing() || isDestroyed()) return;
                        steamFrameResourcesReady = true;
                        advanceFirstLaunchSetup();
                    });
                } catch (Exception failure) {
                    nativeCancelSteamFrameStartup();
                    showSteamFrameStartupErrorFromNative("App data preparation failed: " + failure);
                }
            }, "SM64VR-AppData").start();
            return;
        }
        installBundledResources();
        normalizeCustomPaletteNames();
        super.onCreate(savedInstanceState);
        advanceFirstLaunchSetup();
    }

    @Override
    protected void onResume() {
        super.onResume();
        steamFrameResumed = true;
        if (storageAccessPromptActive) {
            storageAccessPromptActive = false;
            storageAccessPromptCompleted = true;
        }
        resumePendingStandaloneInstall();
        if (BuildConfig.STEAM_FRAME && steamFrameBrowserAfterStorageRequest) {
            steamFrameBrowserAfterStorageRequest = false;
            showSteamFrameRomBrowser();
        }
        if (microphonePermissionPending) return;
        advanceFirstLaunchSetup();
    }

    @Override protected void onPause() {
        steamFrameResumed = false;
        super.onPause();
    }

    private File appDataRoot() {
        return BuildConfig.STEAM_FRAME ? steamFrameDataRoot : getExternalFilesDir(null);
    }

    public void onSteamFrameRomValidated(int attempt, boolean accepted, String detail) {
        if (!BuildConfig.STEAM_FRAME) return;
        runOnUiThread(() -> {
            if (isFinishing() || isDestroyed() || !steamFrameStartup.complete(attempt, accepted)) return;
            if (accepted) {
                firstLaunchSetupComplete = true;
                nativeConfirmRomReady(attempt);
            } else {
                firstLaunchSetupComplete = false;
                showSteamFrameMenu("ROM could not be opened — select supported US .z64",
                        new String[] { "Select ROM", "Close game" }, new Runnable[] { () -> {
                            romPickerRequested = false;
                            showSteamFrameRomBrowser();
                        }, this::finish });
            }
        });
    }

    private void watchSteamFrameRomHandshake(int attempt) {
        final Handler handler = new Handler(getMainLooper());
        handler.postDelayed(new Runnable() {
            long previous = android.os.SystemClock.elapsedRealtime();
            @Override public void run() {
                if (isFinishing() || isDestroyed() || firstLaunchSetupComplete ||
                        !steamFrameStartup.isPending(attempt)) return;
                long now = android.os.SystemClock.elapsedRealtime();
                // Count only foreground time; file pickers/backgrounding are not failures.
                boolean timedOut = steamFrameStartup.tick(Math.min(1000, now - previous), steamFrameResumed);
                previous = now;
                if (timedOut) {
                    nativeCancelSteamFrameStartup();
                    showSteamFrameStartupErrorFromNative("Native ROM setup did not respond within 20 seconds of foreground time.");
                    return;
                }
                if (steamFrameStartup.isPending()) handler.postDelayed(this, 250);
            }
        }, 250);
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (BuildConfig.STEAM_FRAME && requestCode == STEAM_FRAME_STORAGE_PERMISSION_REQUEST) {
            showSteamFrameRomBrowser();
            return;
        }
        if (requestCode == MICROPHONE_PERMISSION_REQUEST) {
            microphonePermissionPending = false;
            advanceFirstLaunchSetup();
        }
    }

    private void advanceFirstLaunchSetup() {
        if (firstLaunchSetupComplete) return;
        if (BuildConfig.STEAM_FRAME && (!steamFrameResourcesReady || steamFrameImportPending || steamFrameStartupErrorShown ||
                !steamFrameStartup.canSubmit())) return;
        // The ROM is the required input for the native host. Ask for it before
        // microphone/storage permissions so first-run setup is deterministic.
        File firstRunRom = new File(appDataRoot(), ROM_NAME);
        boolean romReady = firstRunRom.isFile();
        if (BuildConfig.STEAM_FRAME && romReady) {
            try {
                romReady = firstRunRom.length() == 8L * 1024 * 1024 && US_ROM_SHA1.equals(sha1(firstRunRom));
            } catch (Exception exception) {
                Log.w(TAG, "Could not validate the existing Steam Frame ROM.", exception);
                romReady = false;
            }
        }
        if (!romReady) {
            if (!romPickerRequested) {
                romPickerRequested = true;
                if (BuildConfig.STEAM_FRAME) {
                    showSteamFrameRomBrowser();
                    return;
                }
                romPickerLaunchPending = true;
                new Handler(getMainLooper()).postDelayed(
                        this::launchPendingRomPicker, 250);
            }
            return;
        }
        if (BuildConfig.STEAM_FRAME) {
            // Lepton may close the immersive app when it switches Android
            // activities. Only the ROM is required to start the game. Speech
            // already requests microphone permission when the player uses it.
            if (steamFrameRomBrowser != null) steamFrameRomBrowser.dismiss();
            romPickerFallbackDialogVisible = false;
            romPickerAwaitingResult = false;
            romPickerLaunchPending = false;
            prepareSharedContentDirectories();
            // Clear any Quest update notice inherited from a renamed alpha.
            // Alpha updates are manual until a dedicated, validated feed exists.
            saveStandaloneUpdateStatus(true, "");
            int attempt = steamFrameStartup.begin();
            showSteamFrameMenu("Starting game — validating ROM", new String[0], new Runnable[0]);
            nativeOnRomImported(steamFrameDataRoot.getPath(), attempt);
            watchSteamFrameRomHandshake(attempt);
            return;
        }
        if (Build.VERSION.SDK_INT >= 23 &&
                checkSelfPermission(Manifest.permission.RECORD_AUDIO) !=
                    PackageManager.PERMISSION_GRANTED) {
            if (!microphonePermissionPending) {
                microphonePermissionPending = true;
                requestPermissions(new String[] { Manifest.permission.RECORD_AUDIO },
                        MICROPHONE_PERMISSION_REQUEST);
            }
            return;
        }
        if (Build.VERSION.SDK_INT >= 30 &&
                !Environment.isExternalStorageManager() &&
                !storageAccessPromptCompleted) {
            if (!storageAccessPromptActive) {
                storageAccessPromptActive = true;
                requestSharedStorageAccess();
            }
            return;
        }
        prepareSharedContentDirectories();
        if (!standaloneUpdateCheckStarted) {
            standaloneUpdateCheckStarted = true;
            checkForStandaloneUpdate();
        }
        File rom = new File(appDataRoot(), ROM_NAME);
        if (!rom.isFile()) {
            if (!romPickerRequested) {
                romPickerRequested = true;
                romPickerLaunchPending = true;
                // Let the permission/settings activity finish handing focus
                // back to the game before opening the document picker. Quest
                // can drop an activity launched in that same frame.
                new Handler(getMainLooper()).postDelayed(
                        this::launchPendingRomPicker, 250);
            }
            // Never mark first-launch setup complete while the ROM is absent.
            // A later permission/settings resume must still be able to retry.
            return;
        }
        firstLaunchSetupComplete = true;
    }

    private void launchPendingRomPicker() {
        if (!romPickerLaunchPending || isFinishing()) return;
        romPickerLaunchPending = false;
        romPickerAwaitingResult = true;
        try {
            openRomPicker();
            scheduleRomPickerFallback();
        } catch (RuntimeException exception) {
            romPickerAwaitingResult = false;
            romPickerRequested = false;
            Log.e(TAG, "Could not open the ROM picker.", exception);
            showRomPickerFallback();
        }
    }

    private void scheduleRomPickerFallback() {
        if (romPickerFallbackScheduled) return;
        romPickerFallbackScheduled = true;
        new Handler(getMainLooper()).postDelayed(() -> {
            romPickerFallbackScheduled = false;
            if (romPickerAwaitingResult && !isFinishing() && hasWindowFocus()) {
                if (!trySteamFrameContentPicker()) showRomPickerFallback();
            }
        }, 1500);
    }

    private void showRomPickerFallback() {
        if (BuildConfig.STEAM_FRAME) {
            showSteamFrameRomBrowser();
            return;
        }
        if (isFinishing() || romPickerFallbackDialogVisible) return;
        romPickerFallbackDialogVisible = true;
        new AlertDialog.Builder(this)
                .setTitle("ROM required")
                .setMessage("Select your supported US Super Mario 64 ROM to continue.")
                .setCancelable(false)
                .setPositiveButton("Select ROM", (dialog, which) -> {
                    romPickerFallbackDialogVisible = false;
                    romPickerRequested = true;
                    romPickerAwaitingResult = true;
                    try {
                        openRomPicker();
                        scheduleRomPickerFallback();
                    } catch (RuntimeException exception) {
                        romPickerAwaitingResult = false;
                        romPickerRequested = false;
                        Log.e(TAG, "Could not open the ROM picker.", exception);
                        showUpdateToast("Could not open the ROM picker. Try again.");
                    }
                })
                .show();
    }

    public void downloadLatestStandaloneUpdateFromNative() {
        if (BuildConfig.STEAM_FRAME) {
            showUpdateToast("Steam Frame alpha updates must be installed manually from the Steam Frame alpha release page. Quest updates are disabled.");
            return;
        }
        if (standaloneUpdateInProgress) return;
        standaloneUpdateInProgress = true;
        Thread updater = new Thread(() -> {
            try {
                String assetUrl = findLatestStandaloneApk();
                if (assetUrl == null) {
                    showUpdateToast("No Quest APK was found in the latest release.");
                    standaloneUpdateInProgress = false;
                    return;
                }
                installApkFromUrl(assetUrl);
            } catch (Exception exception) {
                Log.e(TAG, "Standalone update download failed", exception);
                showUpdateToast("Update download failed. Check your connection and try again.");
                standaloneUpdateInProgress = false;
            }
        }, "SM64VR-StandaloneUpdater");
        updater.setDaemon(true);
        updater.start();
    }

    private String findLatestStandaloneApk() throws Exception {
        if (BuildConfig.STEAM_FRAME) return null;
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
        if (BuildConfig.STEAM_FRAME) {
            throw new IOException("Automatic APK installation is disabled for the Steam Frame alpha.");
        }
        if (Build.VERSION.SDK_INT >= 26 && !getPackageManager().canRequestPackageInstalls()) {
            pendingStandaloneUpdateUrl = assetUrl;
            standaloneInstallPermissionPromptActive = true;
            Intent settings = new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                    Uri.parse("package:" + getPackageName()));
            // Keep this as an activity-for-result launch. Quest sometimes
            // resumes NativeActivity without delivering a normal onResume
            // edge when the install-source screen is opened as a new task.
            runOnUiThread(() -> {
                try {
                    startActivityForResult(settings, INSTALL_PERMISSION_REQUEST);
                } catch (RuntimeException exception) {
                    Log.e(TAG, "Could not open install permission settings.", exception);
                    showUpdateToast("Open Android settings and allow installs for SM64 Co-Op DX VR.");
                }
            });
            showUpdateToast("Allow installs for SM64 Co-Op DX VR. The update will resume automatically.");
            standaloneUpdateInProgress = false;
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
        standaloneUpdateInProgress = false;
    }

    private void resumePendingStandaloneInstall() {
        if (!standaloneInstallPermissionPromptActive || pendingStandaloneUpdateUrl == null) {
            return;
        }
        if (Build.VERSION.SDK_INT >= 26 && !getPackageManager().canRequestPackageInstalls()) {
            // Quest may report the permission a moment after the settings
            // activity hands focus back. Retry once after the settings return;
            // a later onResume will retry again if the player grants it then.
            if (!standalonePermissionRetryScheduled) {
                standalonePermissionRetryScheduled = true;
                new Handler(getMainLooper()).postDelayed(() -> {
                    standalonePermissionRetryScheduled = false;
                    resumePendingStandaloneInstall();
                }, 300);
            }
            return;
        }
        String assetUrl = pendingStandaloneUpdateUrl;
        pendingStandaloneUpdateUrl = null;
        standaloneInstallPermissionPromptActive = false;
        standalonePermissionRetryScheduled = false;
        if (standaloneUpdateInProgress) return;
        standaloneUpdateInProgress = true;
        Thread updater = new Thread(() -> {
            try {
                installApkFromUrl(assetUrl);
            } catch (Exception exception) {
                Log.e(TAG, "Resumed standalone update failed", exception);
                showUpdateToast("Update download failed. Check your connection and try again.");
                standaloneUpdateInProgress = false;
            }
        }, "SM64VR-StandaloneUpdaterResume");
        updater.setDaemon(true);
        updater.start();
    }

    private void showUpdateToast(String message) {
        runOnUiThread(() -> Toast.makeText(this, message, Toast.LENGTH_LONG).show());
    }


    public void startSpeechRecognitionFromNative() {
        runOnUiThread(this::toggleSpeechRecognition);
    }

    public void stopSpeechRecognitionFromNative() {
        runOnUiThread(() -> {
            if (!speechListening || speechRecognizer == null) return;
            try {
                // Keep the native session alive until onResults/onError.
                speechRecognizer.stopListening();
            } catch (RuntimeException exception) {
                speechListening = false;
                nativeOnSpeechRecognitionState(false);
                Log.e(TAG, "Could not finish dictation", exception);
            }
        });
    }

    public void cancelSpeechRecognitionFromNative() {
        runOnUiThread(() -> {
            speechStartPendingAfterModelDownload = false;
            speechListening = false;
            if (speechRecognizer != null) speechRecognizer.cancel();
            nativeOnSpeechRecognitionState(false);
        });
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
                    if (BuildConfig.STEAM_FRAME) {
                        nativeOnSpeechRecognitionState(false);
                        Toast.makeText(this, "No Android dictation service is installed. Use the VR keyboard instead.",
                                Toast.LENGTH_LONG).show();
                        return;
                    }
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
                    if (!speechListening) return; // A cancelled keyboard must not receive late text.
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
        if (steamFramePreparation != null) {
            steamFramePreparation.dismiss();
            steamFramePreparation = null;
        }
        if (steamFrameRomBrowser != null) steamFrameRomBrowser.dismiss();
        if (speechRecognizer != null) {
            speechRecognizer.destroy();
            speechRecognizer = null;
        }
        speechListening = false;
        nativeOnSpeechRecognitionState(false);
        super.onDestroy();
    }

    public boolean requestSteamFrameMicrophonePermissionFromNative() {
        if (!BuildConfig.STEAM_FRAME) return false;
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) {
            return true;
        }
        runOnUiThread(() -> {
            if (isFinishing() || steamFrameVoicePermissionRequested || microphonePermissionPending) return;
            steamFrameVoicePermissionRequested = true;
            microphonePermissionPending = true;
            requestPermissions(new String[] { Manifest.permission.RECORD_AUDIO },
                    MICROPHONE_PERMISSION_REQUEST);
        });
        return false;
    }

    public void showSteamFrameStartupErrorFromNative(String detail) {
        if (!BuildConfig.STEAM_FRAME) return;
        runOnUiThread(() -> {
            if (isFinishing() || steamFrameStartupErrorShown) return;
            steamFrameStartupErrorShown = true;
            if (steamFramePreparation != null) {
                steamFramePreparation.dismiss();
                steamFramePreparation = null;
            }
            showSteamFrameMenu("Startup failed: " + detail, new String[] { "Close game" }, new Runnable[] { this::finish });
            // Preserve an Android error panel as well if XR itself is unavailable.
            new AlertDialog.Builder(this)
                    .setTitle("Steam Frame could not start VR")
                    .setMessage(detail + "\n\nClose the app and relaunch it from the headset using the Android/Lepton runtime. " +
                            "Your imported ROM, saves and settings are preserved. " +
                            "This alpha requires a compatible OpenXR OpenGL ES runtime.")
                    .setCancelable(false)
                    .setPositiveButton("Close", (dialog, which) -> finish())
                    .show();
        });
    }

    public void showSteamFrameAudioErrorFromNative(String unused) {
        if (!BuildConfig.STEAM_FRAME) return;
        runOnUiThread(() -> {
            if (isFinishing() || steamFrameStartupErrorShown || steamFrameAudioErrorShown) return;
            steamFrameAudioErrorShown = true;
            new AlertDialog.Builder(this)
                    .setTitle("Headset audio unavailable")
                    .setMessage("Android audio output could not start or was disconnected. Check the headset's audio output. " +
                            "You can retry sound or continue without it.")
                    .setCancelable(false)
                    .setPositiveButton("Retry audio", (dialog, which) -> {
                        steamFrameAudioErrorShown = false;
                        nativeRetryAudio();
                    })
                    .setNegativeButton("Continue without sound", (dialog, which) -> { })
                    .show();
        });
    }

    private void showSteamFrameRomBrowser() {
        if (!BuildConfig.STEAM_FRAME || isFinishing() || firstLaunchSetupComplete ||
                steamFrameImportPending || steamFrameStartup.isPending() || steamFrameStartupErrorShown) return;
        if (romPickerFallbackDialogVisible) return;
        romPickerAwaitingResult = false;
        romPickerLaunchPending = false;
        romPickerFallbackDialogVisible = true;
        steamFrameRomBrowser = new SteamFrameRomBrowser(this, appDataRoot(), new SteamFrameRomBrowser.Listener() {
            private void clearBrowser() {
                romPickerFallbackDialogVisible = false;
                if (steamFrameRomBrowser != null) steamFrameRomBrowser.dismiss();
            }
            @Override public void onRom(File file) {
                clearBrowser();
                importRom(Uri.fromFile(file));
            }
            @Override public void onSystemPicker() {
                clearBrowser();
                steamFrameContentPickerAttempted = false;
                romPickerRequested = true;
                romPickerLaunchPending = true;
                launchPendingRomPicker();
            }
            @Override public void onStorageAccess() {
                clearBrowser();
                if (Build.VERSION.SDK_INT <= 32) {
                    requestPermissions(new String[] { Manifest.permission.READ_EXTERNAL_STORAGE,
                            Manifest.permission.WRITE_EXTERNAL_STORAGE }, STEAM_FRAME_STORAGE_PERMISSION_REQUEST);
                } else {
                    steamFrameBrowserAfterStorageRequest = true;
                    boolean opened = openSteamFrameStorageSettings();
                    // Settings handlers are optional too. Restore the browser
                    // even if neither all-files settings intent exists.
                    if (!opened) {
                        steamFrameBrowserAfterStorageRequest = false;
                        showSteamFrameRomBrowser();
                    }
                }
            }
            @Override public void onClose() {
                clearBrowser();
                finish();
            }
        });
        steamFrameRomBrowser.show();
    }

    private boolean openSteamFrameStorageSettings() {
        try {
            startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:" + getPackageName())));
            return true;
        } catch (RuntimeException exception) {
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                return true;
            } catch (RuntimeException unavailable) {
                showUpdateToast("Storage settings are unavailable. Use SM64 app files or a system picker grant instead.");
                return false;
            }
        }
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
        File paletteDirectory = new File(appDataRoot(), "palettes");
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
        if (BuildConfig.STEAM_FRAME) {
            saveStandaloneUpdateStatus(true, "");
            return;
        }
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
        File root = appDataRoot();
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
        File root = appDataRoot();
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
            // Mod directories belong to the player, even when their names
            // match an obsolete bundled mod. Never delete them on startup
            // or update: names alone do not establish asset ownership.
            copyMissingAssetDirectory("mods", new File(root, "mods"));
            Log.i(TAG, "Bundled session mods installed.");
            copyAssetDirectory("sonic_shoes", new File(root, "sonic_shoes"));
            Log.i(TAG, "Bundled Sonic Shoes music installed.");
            copyAssetFile("normal_maps.bin", new File(root, "normal_maps.bin"));
            Log.i(TAG, "Bundled normal maps installed.");
        } catch (IOException exception) {
            Log.e(TAG, "Could not install bundled resources.", exception);
            if (BuildConfig.STEAM_FRAME) throw new IllegalStateException("Bundled resources could not be installed", exception);
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
        if (BuildConfig.STEAM_FRAME) {
            // A headset task can be closed during preparation. Never leave a
            // partial bundled mod looking like an already-installed user file.
            File temporary = new File(parent, destination.getName() + ".sm64-bundled.tmp");
            try {
                try (InputStream input = getAssets().open(assetPath);
                     FileOutputStream output = new FileOutputStream(temporary)) {
                    byte[] bytes = new byte[16 * 1024];
                    int count;
                    while ((count = input.read(bytes)) >= 0) {
                        if (count > 0) output.write(bytes, 0, count);
                    }
                }
                android.system.Os.rename(temporary.getPath(), destination.getPath());
            } catch (android.system.ErrnoException exception) {
                throw new IOException("Could not finish bundled asset " + assetPath, exception);
            } finally {
                temporary.delete();
            }
            return;
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
        intent.setType(BuildConfig.STEAM_FRAME ? "*/*" : "application/octet-stream");
        intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {
                "application/octet-stream", "application/x-n64-rom", "*/*"
        });
        if (!BuildConfig.STEAM_FRAME) {
            startActivityForResult(intent, OPEN_ROM_REQUEST);
            return;
        }
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        try {
            startActivityForResult(intent, OPEN_ROM_REQUEST);
        } catch (ActivityNotFoundException exception) {
            Log.w(TAG, "Document picker unavailable; trying the Android content picker.", exception);
            openSteamFrameContentPicker();
        }
    }

    private void openSteamFrameContentPicker() {
        steamFrameContentPickerAttempted = true;
        Intent intent = new Intent(Intent.ACTION_GET_CONTENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        startActivityForResult(intent, OPEN_ROM_REQUEST);
    }

    private boolean trySteamFrameContentPicker() {
        if (!BuildConfig.STEAM_FRAME || steamFrameContentPickerAttempted || isFinishing()) {
            return false;
        }
        try {
            romPickerRequested = true;
            romPickerAwaitingResult = true;
            openSteamFrameContentPicker();
            scheduleRomPickerFallback();
            return true;
        } catch (RuntimeException exception) {
            romPickerRequested = false;
            romPickerAwaitingResult = false;
            Log.w(TAG, "Android content picker unavailable.", exception);
            return false;
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == INSTALL_PERMISSION_REQUEST) {
            // The permission setting is committed before this result is
            // delivered, so resume the cached APK install immediately.
            resumePendingStandaloneInstall();
            return;
        }
        if (requestCode != OPEN_ROM_REQUEST) {
            return;
        }
        romPickerAwaitingResult = false;
        romPickerRequested = false;
        if (resultCode == RESULT_OK && data != null && data.getData() != null) {
            importRom(data.getData());
        } else {
            if (trySteamFrameContentPicker()) return;
            showRomPickerFallback();
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            // Fallback for Quest builds that do not deliver an activity result
            // from the install-source settings screen.
            new Handler(getMainLooper()).postDelayed(
                    this::resumePendingStandaloneInstall, 100);
            if (!firstLaunchSetupComplete && !romPickerAwaitingResult &&
                    !romPickerFallbackDialogVisible) {
                new Handler(getMainLooper()).postDelayed(
                        this::advanceFirstLaunchSetup, 100);
            }
        }
    }

    private void importRom(Uri uri) {
        if (BuildConfig.STEAM_FRAME) {
            importSteamFrameRom(uri);
            return;
        }
        if (BuildConfig.STEAM_FRAME && (firstLaunchSetupComplete ||
                steamFrameStartup.isPending() || steamFrameStartupErrorShown)) return;
        File destination = new File(appDataRoot(), ROM_NAME);
        File temporary = new File(appDataRoot(), ROM_NAME + ".tmp");
        byte[] buffer = new byte[64 * 1024];
        try (InputStream input = getContentResolver().openInputStream(uri);
             FileOutputStream output = new FileOutputStream(temporary)) {
            if (input == null) throw new IllegalStateException("ROM stream unavailable");
            int count;
            long total = 0;
            while ((count = input.read(buffer)) >= 0) {
                total += count;
                if (BuildConfig.STEAM_FRAME && total > 8L * 1024 * 1024) {
                    throw new IOException("The supported US ROM must be an uncompressed 8 MiB .z64 file.");
                }
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
            if (BuildConfig.STEAM_FRAME) {
                // Both files are on the selected app filesystem. POSIX rename
                // replaces atomically; a failed replacement keeps the old ROM.
                android.system.Os.rename(temporary.getPath(), destination.getPath());
            } else {
                if (destination.exists() && !destination.delete()) {
                    throw new IllegalStateException("Could not replace existing ROM");
                }
                if (!temporary.renameTo(destination)) {
                    throw new IllegalStateException("Could not finish ROM import");
                }
            }
            markShaderWarmupPending();
            Log.i(TAG, "US SM64 ROM imported successfully from Android file picker.");
            Toast.makeText(this, "SM64 US ROM imported successfully.", Toast.LENGTH_LONG).show();
            if (BuildConfig.STEAM_FRAME) {
                // Keep the same NativeActivity and process. Restarting the
                // activity races android_main's process-exit cleanup and can
                // close Lepton immediately after a successful ROM import.
                // Do not unblock a rejected attempt merely by resuming or
                // opening the browser: only a new validated import can retry.
                steamFrameStartup.retry();
                advanceFirstLaunchSetup();
                return;
            }
            // The native OpenXR host checks the ROM once during startup. A
            // first-run picker returns after that check, so recreate the
            // activity to let the native host validate and boot the newly
            // imported ROM without requiring the player to quit manually.
            Intent launch = getPackageManager().getLaunchIntentForPackage(getPackageName());
            if (launch != null) {
                launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
                startActivity(launch);
            }
            finish();
        } catch (Exception exception) {
            temporary.delete();
            reportImportError("Could not validate the selected ROM.", exception);
        }
    }

    private void importSteamFrameRom(Uri uri) {
        if (isFinishing() || isDestroyed() || steamFrameImportPending || firstLaunchSetupComplete ||
                !steamFrameResourcesReady || steamFrameStartup.isPending() || steamFrameStartupErrorShown) return;
        steamFrameImportPending = true;
        final File root = appDataRoot();
        showSteamFrameMenu("Copying and validating ROM — please wait", new String[0], new Runnable[0]);
        new Thread(() -> {
            File temporary = new File(root, ROM_NAME + ".tmp");
            Exception failure = null;
            try {
                // Content providers may be slow: never block Android's lifecycle
                // thread, which must process picker return and NativeActivity events.
                try (InputStream input = getContentResolver().openInputStream(uri);
                     FileOutputStream output = new FileOutputStream(temporary)) {
                    if (input == null) throw new IOException("ROM stream unavailable");
                    byte[] buffer = new byte[64 * 1024];
                    long total = 0;
                    int count;
                    while ((count = input.read(buffer)) != -1) {
                        total += count;
                        if (total > 8L * 1024 * 1024) throw new IOException("Select the uncompressed 8 MiB US .z64 ROM.");
                        if (count > 0) output.write(buffer, 0, count);
                    }
                    if (total != 8L * 1024 * 1024) throw new IOException("Select the uncompressed 8 MiB US .z64 ROM.");
                    output.getFD().sync();
                }
                if (!US_ROM_SHA1.equals(sha1(temporary))) throw new IOException("That is not the supported US SM64 ROM.");
                android.system.Os.rename(temporary.getPath(), new File(root, ROM_NAME).getPath());
                markShaderWarmupPending();
            } catch (Exception exception) {
                temporary.delete();
                failure = exception;
            }
            final Exception result = failure;
            runOnUiThread(() -> {
                steamFrameImportPending = false;
                if (isFinishing() || isDestroyed()) return;
                if (result != null) {
                    Log.w(TAG, "Could not import ROM", result);
                    showSteamFrameMenu(result.getMessage(), new String[] { "Select ROM", "Close game" },
                            new Runnable[] { this::showSteamFrameRomBrowser, this::finish });
                    return;
                }
                Log.i(TAG, "Steam Frame ROM imported; continuing in the same activity.");
                steamFrameStartup.retry();
                advanceFirstLaunchSetup();
            });
        }, "SM64VR-RomImport").start();
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
        if (BuildConfig.STEAM_FRAME) showRomPickerFallback();
    }
}
