# Steam Frame alpha target

Build this target explicitly; renaming the Quest APK does not enable it:

```powershell
.\gradlew.bat :app:assembleDebug -Psm64Target=steamFrame --offline
```

The output is `app/build-steam-frame/outputs/apk/debug/app-debug.apk`.
Native intermediate files use `app/.cxx-steam-frame`. The default target
remains `quest` and retains its existing startup behavior and output paths.
The Steam Frame target reads generated assets from this source tree's
`build/us_pc`. An existing alternate generated-asset directory can be supplied
with `-Psm64GeneratedRoot=...`.

On a fresh Steam Frame install, choose the ROM from the app's OpenXR file browser
inside the headset. Use either thumbstick up/down and either trigger or A/X to
select. The setup panel starts OpenXR before ROM selection, because Steam-launched
Lepton VR applications normally hide Android windows. Game initialization waits
until both Java and native code validate the supported US ROM. The optional
System picker uses Open Document and falls back to Get Content, but needs a
visible Android development panel; normal setup does not depend on that service.
Import continues in the same activity and
process; it does not relaunch the activity or finish the task. A bad ROM can
be selected again. Microphone permission is requested when dictation is used,
rather than interrupting this first-launch flow. Shared storage can still be
enabled in Android's app settings; app-private data does not require it.

The default browser stays in the game's activity and uses a stereo OpenXR quad,
without requiring an Android window or the host `lepton-show-flatscreen` marker.
The panel texture is uploaded only when its menu changes, once per swapchain image.
Choose Downloads, Shared storage, or SM64 app files. Frame targets Android API 29
with legacy storage enabled and declares read/write storage permissions through
API 32. This covers Lepton's Android 11 (API 30) and its installation-time runtime
permission grant. Valve's mounting code maps host Downloads into Android Download.
Storage access is optional: Android through API 32 can request legacy read/write
access, while newer Android can open all-files access settings. If those
settings are unavailable, app files and granted system-picker URIs still work.
Only the supported, uncompressed 8 MiB US .z64 ROM passes validation. A wrong
file leaves any previously imported ROM untouched and returns to selection.

The alpha does not check for, download, or install Quest updates. Install
future Steam Frame alphas manually from their separate release page. The
package identity remains unchanged to preserve existing app-private data.

Startup failures show the failed stage and a Close action rather than silently
leaving a blank app running. Audio tries AAudio and OpenSL; a silent/null device
is not accepted. If output fails or disconnects, Retry audio requests a reopen
on the native game thread. Continuing without sound is also possible.
Voice chat requests microphone permission independently of keyboard dictation.
Dictation uses a standard Android recognizer when one is available; it does
not try to launch the Oculus service on Steam Frame. With no recognizer, the
VR keyboard remains available and the app explains why dictation cannot run.

The launcher is the game NativeActivity itself. The temporary launcher used
by the previous alpha is now an activity alias to that same game activity,
preserving explicit shortcuts without starting and finishing a second activity.
Native libraries are compressed in the APK and extracted by Android into its
executable app directory. This avoids direct ELF loading from an APK located
on shared or microSD storage. ROM copying and validation run on a worker thread
with a visible headset progress panel; returning from a slow file provider cannot block
Android's UI thread during the import. A valid import continues in place.

For Steam shortcuts, the executable must be the APK and the compatibility
runtime must be Android/Lepton, not Proton. The app cannot configure a shortcut
that never starts Android. Valve's developer deployment workflow explicitly
selects the Android runtime. Hardware validation of right-click Add to Steam
remains necessary, including shortcuts retained from an older alpha.

Frame startup and rendering do not wait for an Android 2D native window: EGL
uses a pbuffer and OpenXR owns the headset surfaces. ROM setup is a two-way
Java/native handshake with attempt IDs. A native rejection offers Select ROM
or Close; resuming the app cannot silently retry the rejected file. A lost
acknowledgement times out after 20 seconds of foreground time. Failure to
enter the first running XR session is reported after 30 foreground seconds.
Normal backgrounding/file selection is not counted as an error.

The selected app-private data directory is remembered. A new install can use
internal app files if external app files are unavailable; later launches keep
using that root. If an already-selected external root disappears, startup
reports it instead of silently switching saves to an empty directory. Java
imports, bundled resources, native ROM validation and game saves use the same
root. Resource preparation runs off the UI thread; bundled files and validated
ROM replacements are committed with same-filesystem atomic renames. No source
ROM, save, settings or user mod is removed by these startup changes.
An upgrade from an older alpha without a remembered root also refuses to
silently fall back when external app storage is unavailable.

Valve documents Oculus Touch emulation as the default for Frame controllers,
so this target retains the existing Touch bindings. It does not alter Mario's
NPC dialogs, cutscenes, movement, rendering, saves, or the Quest/PC builds.

References:

- [Valve custom-engine guidance](https://partner.steamgames.com/doc/steamhardware/steamframe/engines/custom?l=english)
- [Valve Lepton debugging](https://partner.steamgames.com/doc/steamhardware/steamframe/debugging)

Hardware validation is still required on Steam Frame: fresh launch, cancel
and retry, invalid ROM, valid ROM import into VR, cold relaunch, controller
input, audio, and game exit. Do not label the alpha as tested based solely on
local compilation.
