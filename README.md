# SM64 Co-Op DX VR

An experimental vibe coded OpenXR VR fork of [SM64 Co-Op DX](https://github.com/coop-deluxe/sm64coopdx).

The current playable milestone provides **third-person and experimental first-person VR modes designed for a standard gamepad**. It keeps Mario's original movement and gameplay while rendering the game in stereoscopic 6DoF VR. Third Person Mode supports both the default camera and SM64 Co-Op DX's free camera; First Person Mode (Experimental) adds head-directed movement and smooth horizontal turning.

> [!IMPORTANT]
> This is an early development fork, not a finished release. The current VR mode targets Windows and OpenGL. First Person Mode is an initial experimental implementation and motion-controller gameplay is still planned.
> Coop is currently untested, solo hosting is tested and works.
> I recommend adjusting camera distance to your liking in the VR menu, as well as testing if you prefer default or free camera.

## Current features

- OpenXR headset and runtime detection
- Native stereoscopic rendering into separate left- and right-eye OpenXR swapchains
- Runtime-provided per-eye position, rotation, field of view, and projection
- 6DoF headset tracking for looking, leaning, and positional head movement
- Playable third-person gamepad controls
- Initial **First Person Mode (Experimental)** with gamepad controls and head-directed movement
- Compatibility with both the default camera and free camera
- A dedicated **VR** settings menu
- A **Camera Settings** submenu with Third Person Mode, First Person Mode (Experimental), camera distance, camera height, field of view, and movement-direction calibration
- Smooth horizontal turning in First Person Mode
- An **Experimental** submenu with optional smooth 180-degree backflip/side-flip camera turns and flat-screen first-person support
- A **Performance** submenu with render-scale and desktop-mirror frame-rate controls
- Adjustable HUD opacity
- Optional automatic VR startup
- VR-aware rendering and culling so scenery remains visible while looking around
- Corrected skybox, lighting, terrain, and billboard behavior for stereo head movement
- Head-locked main menu, pause menu, HUD, text boxes, and star-select UI
- Left-eye desktop mirroring for recording and streaming VR gameplay
- Flat-screen mode remains available when VR mode is disabled

## Download and play on Windows

No compiler, Git, MSYS2, or command-line setup is required for a normal player release.

1. Open the [latest release](https://github.com/fulldivegames/sm64coopdx-VR/releases/latest) and download the Windows ZIP.
2. Extract the entire ZIP to a normal folder. Do not launch the game from inside the ZIP.
3. Start your headset software and make sure your preferred OpenXR runtime is active.
4. Connect a standard gamepad.
5. Launch `SM64-Co-Op-DX-VR.exe`.
6. On first launch, drag your legally obtained, unmodified **Super Mario 64 US `.z64` ROM** onto the game window when prompted.
7. Open **Settings > VR**, enable **VR Mode**, then use **Camera Settings** to select Third Person Mode or First Person Mode (Experimental) and adjust its settings.

The game validates the ROM and remembers it in the user-data folder. It should not need to be dragged into the window on every launch.

> [!IMPORTANT]
> The release ZIP does not contain a ROM or Nintendo game assets. You must provide your own legally obtained, unmodified US ROM. Do not upload ROMs when reporting a problem.

If Windows SmartScreen appears, confirm that the ZIP came from this repository's official release page. This community build is not currently code-signed.

### Player requirements

- A 64-bit Windows PC
- A PC VR headset with a working OpenXR runtime
- OpenGL support compatible with the active OpenXR runtime
- A standard gamepad
- A legally obtained, unmodified Super Mario 64 US ROM

The current development build has been tested with a Meta Quest 3 using Virtual Desktop's OpenXR runtime. Other conformant OpenXR headsets and runtimes may work, but have not all been tested yet. Coop play and rom hacks have also not been tested.

## Building from source (developers only)

Players should use the release ZIP above. Building is only necessary if you want to modify the source or test an unreleased commit.

### Developer requirements

- The normal [SM64 Co-Op DX build prerequisites](https://github.com/coop-deluxe/sm64coopdx)
- A Windows/MSYS2 MinGW64 build environment
- Git and GNU Make

After installing the upstream Windows build prerequisites, clone this fork, check out the `vr` branch, and build it from an **MSYS2 MinGW64** shell:

```sh
git clone --branch vr https://github.com/fulldivegames/sm64coopdx-VR.git
cd sm64coopdx-VR
make -j
```

The executable is produced at:

```text
build/us_pc/sm64coopdx.exe
```

To create the same allow-listed Windows player ZIP used by releases, run this from PowerShell after a successful build:

```powershell
.\tools\package-vr-windows.ps1 -Version dev
```

The ZIP is written to `dist/`. The packaging script includes only the runtime files needed by players and verifies that no `.z64`, `.n64`, or `.v64` ROM was staged. Nintendo ROMs and game assets must never be committed, uploaded as workflow artifacts, or attached to a release.

## Using VR mode

1. Start your headset and select an active OpenXR runtime before launching the game.
2. Connect a gamepad.
3. Launch `sm64coopdx.exe`.
4. Open the game's settings and enter the **VR** menu.
5. Enable **VR Mode**.
6. Open **Camera Settings**, select **Third Person Mode** or **First Person Mode (Experimental)**, and adjust the available camera settings to your preference.

The game can also be configured to launch in VR automatically. If VR initialization fails, launch with `--console` and check the lines beginning with `[VR]` for the detected loader, runtime, headset, and session state.

## Controls and camera behavior

This milestone is intentionally a seated or standing **gamepad VR** experience. Mario is controlled with the same gamepad bindings used by regular SM64 Co-Op DX, while the headset controls the player's view.

In **Third Person Mode**, both supported camera systems are valid choices:

- **Default camera:** works with the original SM64-style camera behavior.
- **Free camera:** works with SM64 Co-Op DX's free-camera behavior.

Neither camera mode is required or specifically recommended; use whichever you prefer.

In **First Person Mode (Experimental)**, the headset controls Mario's forward direction while the right stick provides smooth horizontal turning. Sideways stick input is restricted to a narrower steering angle for comfort. Camera height, field of view, movement-direction calibration, and performance controls are available in the VR submenus. Mario's model is hidden locally in this mode so his head and body do not obstruct the headset view.

## Current limitations

- Windows/OpenGL is the only implemented OpenXR graphics path.
- Motion controllers, tracked hands, hand interaction, and haptics are not implemented yet.
- First Person Mode is an early implementation and may still have performance or camera edge cases in some levels, actions, mods, and ROM hacks.
- The 3D star models currently do not appear on the Star Select screen in VR. The screen remains usable, but this visual bug is still under investigation.
- Some flat billboard objects, such as original 2D trees, may make their camera-facing rotation more noticeable in VR. True 3D replacement models can improve this in the future.
- Hardware and runtime compatibility testing is still limited.
- General Lua mods, model packs, texture packs, and ROM hacks require broader compatibility testing in VR.

Please include the headset, OpenXR runtime, GPU, reproduction steps, and the relevant `[VR]` console output when reporting a VR-specific bug.

## Planned direction

The long-term goal is to expand this foundation without sacrificing SM64 Co-Op DX's multiplayer and mod support. Planned work includes:

- Permanently floating tracked hands instead of VR arms
- option to hide torso and legs
- Motion-based punching and physical interactions
- Hand anchoring for wall slides and wall jumps
- Hand-mounted HUD placement options on either hand
- HUD size, angle, position, and distance adjustments
- Controller haptics and calibration options
- Smooth switching between VR and flat-screen play (Might work currently)
- Continued multiplayer, Lua mod, model/texture mod, and ROM-hack compatibility work

These are just goals and I'm not sure when I'll have time to work on this

## Project status

The current third-person milestone is considered feature-complete for now. Version 0.2 introduces the first playable version of First Person Mode; development will continue with first-person refinement and the future tracked-hand systems.

## Credits and acknowledgements

- [SM64 Co-Op DX](https://github.com/coop-deluxe/sm64coopdx) and the Coop Deluxe Team for the multiplayer game and codebase this project forks
- [sm64ex-coop](https://github.com/djoslin0/sm64ex-coop) and djoslin0 for the project continued by SM64 Co-Op DX
- The Super Mario 64 decompilation and PC-port contributors whose work underpins the upstream project
- [Khronos OpenXR](https://www.khronos.org/openxr/) for the cross-platform XR API used by this VR implementation

No source code from another SM64 or Zelda VR mod has been incorporated into this milestone. Additional third-party work will only be bundled when its license or author explicitly permits redistribution, and it will be credited if used.

## Legal and licensing notice

This is an unofficial fan project and is not affiliated with or endorsed by Nintendo, the SM64 Co-Op DX team, Meta, Virtual Desktop, or Khronos. Super Mario and related names, characters, and assets are property of their respective owners.

This fork retains the upstream repository's history and third-party notices. The checkout does not currently contain a single project-wide license file, so do not assume that every file or bundled component is covered by one license. Each dependency and third-party component remains subject to its own terms. Do not redistribute Nintendo ROMs or game assets, and obtain permission before bundling third-party mods or model packs.

## Upstream project

SM64 Co-Op DX is an online multiplayer continuation of sm64ex-coop maintained by the Coop Deluxe Team. For upstream documentation, community information, and non-VR issues, visit the [official SM64 Co-Op DX repository](https://github.com/coop-deluxe/sm64coopdx).
