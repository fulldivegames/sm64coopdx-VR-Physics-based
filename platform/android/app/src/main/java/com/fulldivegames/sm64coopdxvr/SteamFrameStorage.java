package com.fulldivegames.sm64coopdxvr;

import android.content.Context;
import android.content.SharedPreferences;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;

/** Select once, then fail visibly rather than silently switching saves between roots. */
final class SteamFrameStorage {
    static File select(Context context) throws IOException {
        SharedPreferences preferences = context.getSharedPreferences("steam_frame_startup", 0);
        String saved = preferences.getString("data_root", null);
        File internal = context.getFilesDir();
        File external = context.getExternalFilesDir(null);
        if (saved != null) {
            File selected = new File(saved).getCanonicalFile();
            // Only app-owned roots may be persisted here, not an arbitrary preference path.
            if (!selected.equals(internal.getCanonicalFile()) &&
                    (external == null || !selected.equals(external.getCanonicalFile()))) {
                throw new IOException("The saved app data location is unavailable. Restore its storage and relaunch; saves have not been moved.");
            }
            checkWritable(selected);
            return selected;
        }
        File selected;
        try {
            if (external == null) throw new IOException("External app storage unavailable");
            checkWritable(external);
            selected = external.getCanonicalFile();
        } catch (IOException unavailable) {
            // Older alphas kept saves externally without this preference. On
            // an upgrade, an unavailable mount must not look like a fresh save.
            try {
                android.content.pm.PackageInfo installed = context.getPackageManager()
                        .getPackageInfo(context.getPackageName(), 0);
                if (installed.lastUpdateTime != installed.firstInstallTime) {
                    throw new IOException("Existing app storage is unavailable. Restore external app storage and relaunch; no saves were moved.", unavailable);
                }
            } catch (android.content.pm.PackageManager.NameNotFoundException exception) {
                throw new IOException("Could not verify the existing app data location", exception);
            }
            checkWritable(internal);
            selected = internal.getCanonicalFile();
        }
        if (!preferences.edit().putString("data_root", selected.getPath()).commit()) {
            throw new IOException("Could not remember the app data location");
        }
        return selected;
    }

    private static void checkWritable(File root) throws IOException {
        if (root == null || (!root.isDirectory() && !root.mkdirs())) {
            throw new IOException("App data directory unavailable");
        }
        File probe = File.createTempFile("sm64-storage-", ".tmp", root);
        try (FileOutputStream output = new FileOutputStream(probe)) {
            output.write(0);
        } finally {
            if (!probe.delete()) probe.deleteOnExit();
        }
    }
}
