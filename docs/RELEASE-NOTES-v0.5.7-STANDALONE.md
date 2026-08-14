# SM64 Co-Op DX VR Quest Standalone v0.5.7

This focused update makes installing large Quest mods much easier.

## Shared mod folder

The primary standalone mod directory is now:

```text
/sdcard/SM64VR/mods/
```

Launch the updated app once and enable **Allow access to manage all files** for SM64 Co-Op DX VR Standalone. Close and reopen the app after granting access. You can then transfer extracted mod folders into `SM64VR/mods` through SideQuest without navigating the restricted `Android/data` directory.

The old private mods directory remains supported as a compatibility fallback. Shared-folder mods take priority when the same mod exists in both locations.

## Install or update

1. Download `SM64-Co-Op-DX-VR-Quest-v0.5.7.apk`.
2. Install it through SideQuest over the existing application. Do not uninstall first if you want to preserve the private ROM, saves, and settings.
3. Launch it once, grant the requested file-access setting, then close and reopen the app.

The APK contains no ROM or Nintendo game assets. Quest 3 is the only standalone headset tested by the project; Quest 2 remains untested.
