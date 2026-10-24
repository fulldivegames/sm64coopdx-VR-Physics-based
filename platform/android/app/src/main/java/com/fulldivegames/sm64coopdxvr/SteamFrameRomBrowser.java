package com.fulldivegames.sm64coopdxvr;

import android.app.Activity;
import android.app.AlertDialog;
import android.os.Environment;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.Locale;

/** App-owned ROM selector: does not require DocumentsUI or a desktop mouse. */
final class SteamFrameRomBrowser {
    interface Presenter {
        void showSteamFrameMenu(String title, String[] names, Runnable[] actions);
    }
    interface Listener {
        void onRom(File file);
        void onSystemPicker();
        void onStorageAccess();
        void onClose();
    }

    private final Activity activity;
    private final Listener listener;
    private final File appFiles;
    private AlertDialog dialog;

    SteamFrameRomBrowser(Activity activity, Listener listener) {
        this(activity, activity.getExternalFilesDir(null), listener);
    }

    SteamFrameRomBrowser(Activity activity, File appFiles, Listener listener) {
        this.activity = activity;
        this.appFiles = appFiles;
        this.listener = listener;
    }

    void dismiss() {
        if (dialog != null) dialog.dismiss();
        dialog = null;
    }

    void show() {
        dismiss();
        if (activity.isFinishing()) return;
        ArrayList<File> locations = new ArrayList<>();
        ArrayList<String> names = new ArrayList<>();
        locations.add(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS));
        names.add("Downloads");
        locations.add(Environment.getExternalStorageDirectory());
        names.add("Shared storage");
        if (appFiles != null) {
            locations.add(appFiles);
            names.add("SM64 app files");
        }
        if (activity instanceof Presenter) {
            ArrayList<Runnable> actions = new ArrayList<>();
            for (File location : locations) actions.add(() -> showDirectory(location, location));
            names.add("Storage access"); actions.add(listener::onStorageAccess);
            names.add("System picker (Android panel)"); actions.add(listener::onSystemPicker);
            names.add("Close game"); actions.add(listener::onClose);
            ((Presenter) activity).showSteamFrameMenu("Select your SM64 US .z64 ROM",
                    names.toArray(new String[0]), actions.toArray(new Runnable[0]));
            return;
        }
        dialog = new AlertDialog.Builder(activity)
                .setTitle("Select SM64 US ROM — choose a location")
                .setItems(names.toArray(new String[0]), (ignored, index) ->
                        showDirectory(locations.get(index), locations.get(index)))
                .setPositiveButton("Storage access", (ignored, which) -> listener.onStorageAccess())
                .setNeutralButton("System picker", (ignored, which) -> listener.onSystemPicker())
                .setNegativeButton("Close", (ignored, which) -> listener.onClose())
                .setCancelable(false)
                .show();
    }

    private void showDirectory(File directory, File root) {
        dismiss();
        if (activity.isFinishing()) return;
        File[] entries;
        try {
            // Stay inside the chosen storage location, including through links.
            String rootPath = root.getCanonicalPath();
            String directoryPath = directory.getCanonicalPath();
            if (!directoryPath.equals(rootPath) && !directoryPath.startsWith(rootPath + File.separator)) {
                show();
                return;
            }
            entries = directory.listFiles(file -> file.isDirectory() || isRomName(file.getName()));
        } catch (Exception exception) {
            entries = null;
        }
        if (entries == null) {
            if (activity instanceof Presenter) {
                ((Presenter) activity).showSteamFrameMenu("Location unreadable — choose access or another location",
                        new String[] { "Storage access", "Locations", "System picker (Android panel)", "Close game" },
                        new Runnable[] { listener::onStorageAccess, this::show, listener::onSystemPicker, listener::onClose });
                return;
            }
            dialog = new AlertDialog.Builder(activity)
                    .setTitle("This location is not readable")
                    .setMessage("Use Storage access to allow file browsing, choose the system picker, " +
                            "or place your supported US .z64 ROM in the app's files directory. " +
                            "No ROM is included with the game.")
                    .setPositiveButton("Storage access", (ignored, which) -> listener.onStorageAccess())
                    .setNeutralButton("Locations", (ignored, which) -> show())
                    .setNegativeButton("System picker", (ignored, which) -> listener.onSystemPicker())
                    .setCancelable(false)
                    .show();
            return;
        }
        Arrays.sort(entries, Comparator.comparing((File file) -> !file.isDirectory())
                .thenComparing(File::getName, String.CASE_INSENSITIVE_ORDER));
        final File[] files = entries;
        boolean hasParent = !directory.equals(root);
        int offset = hasParent ? 1 : 0;
        String[] names = new String[files.length + offset];
        if (hasParent) names[0] = "[..]";
        for (int i = 0; i < files.length; i++) {
            names[i + offset] = files[i].isDirectory()
                    ? "[" + files[i].getName() + "]" : files[i].getName();
        }
        if (activity instanceof Presenter) {
            ArrayList<String> items = new ArrayList<>(Arrays.asList(names));
            ArrayList<Runnable> actions = new ArrayList<>();
            if (hasParent) actions.add(() -> showDirectory(directory.getParentFile(), root));
            for (File file : files) actions.add(() -> select(file, root));
            items.add("Locations"); actions.add(this::show);
            items.add("Storage access"); actions.add(listener::onStorageAccess);
            items.add("Close game"); actions.add(listener::onClose);
            ((Presenter) activity).showSteamFrameMenu(directory.getAbsolutePath(),
                    items.toArray(new String[0]), actions.toArray(new Runnable[0]));
            return;
        }
        dialog = new AlertDialog.Builder(activity)
                .setTitle(directory.getAbsolutePath() + (files.length == 0 ? " — no ROM files" : ""))
                .setItems(names, (ignored, index) -> {
                    if (hasParent && index == 0) {
                        showDirectory(directory.getParentFile(), root);
                        return;
                    }
                    File selected = files[index - offset];
                    if (selected.isDirectory()) showDirectory(selected, root);
                    else {
                        try {
                            String rootPath = root.getCanonicalPath();
                            String filePath = selected.getCanonicalPath();
                            if (!filePath.startsWith(rootPath + File.separator)) {
                                show();
                                return;
                            }
                        } catch (Exception exception) {
                            show();
                            return;
                        }
                        listener.onRom(selected);
                    }
                })
                .setPositiveButton("Locations", (ignored, which) -> show())
                .setNeutralButton("Storage access", (ignored, which) -> listener.onStorageAccess())
                .setNegativeButton("Close", (ignored, which) -> listener.onClose())
                .setCancelable(false)
                .show();
    }

    private void select(File file, File root) {
        try {
            String rootPath = root.getCanonicalPath();
            String path = file.getCanonicalPath();
            if (!path.equals(rootPath) && !path.startsWith(rootPath + File.separator)) { show(); return; }
            if (file.isDirectory()) showDirectory(file, root);
            else listener.onRom(file);
        } catch (Exception failure) { show(); }
    }

    private static boolean isRomName(String name) {
        String lower = name.toLowerCase(Locale.ROOT);
        return lower.endsWith(".z64") || lower.endsWith(".n64") ||
                lower.endsWith(".v64") || lower.endsWith(".rom") || lower.endsWith(".bin");
    }
}
