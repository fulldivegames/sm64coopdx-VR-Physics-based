# SM64 Co-Op DX VR

An experimental vibe coded OpenXR VR fork of [SM64 Co-Op DX](https://github.com/coop-deluxe/sm64coopdx).

The current playable milestone provides a **third-person VR mode designed for a standard gamepad**. It keeps Mario's original movement and gameplay while rendering the game in stereoscopic 6DoF VR. Both the default camera and SM64 Co-Op DX's free camera are supported.

> [!IMPORTANT]
> This is an early development fork, not a finished release. The current VR mode targets Windows and OpenGL. Motion-controller gameplay, first-person mode is planned.
> Coop is currently untested, solo hosting is tested and works.
> I recommend adjusting camera distance to your liking in the VR menu, as well as testing if you prefer default or free camera.

## Current features

- OpenXR headset and runtime detection
- Native stereoscopic rendering into separate left- and right-eye OpenXR swapchains
- Runtime-provided per-eye position, rotation, field of view, and projection
- 6DoF headset tracking for looking, leaning, and positional head movement
- Playable third-person gamepad controls
- Compatibility with both the default camera and free camera
- A dedicated **VR** settings menu
- A **Camera Settings** submenu with Third Person Mode and an adjustable camera-distance slider
- Optional automatic VR startup
- VR-aware rendering and culling so scenery remains visible while looking around
- Corrected skybox, lighting, terrain, and billboard behavior for stereo head movement
- Head-locked main menu, pause menu, HUD, text boxes, and star-select UI
- Flat-screen mode remains available when VR mode is disabled

## Requirements

### To play

- A 64-bit Windows PC
- A PC VR headset with a working OpenXR runtime
- An OpenXR loader available to the game
- OpenGL support compatible with the active OpenXR runtime
- A standard gamepad
- SM64 Co-Op DX's normal game files and legally obtained Super Mario 64 assets

The current development build has been tested with a Meta Quest 3 using Virtual Desktop's OpenXR runtime. Other conformant OpenXR headsets and runtimes may work, but have not all been tested yet. Coop play and rom hacks have also not been tested.

### To build from source

- The normal [SM64 Co-Op DX build prerequisites](https://github.com/coop-deluxe/sm64coopdx)
- A legally obtained Super Mario 64 US ROM
- A Windows/MSYS2 MinGW64 build environment
- Git and GNU Make

Nintendo game assets and ROM files are **not** included in this repository and must not be uploaded to GitHub or attached to a release.

## Building on Windows

Follow the upstream SM64 Co-Op DX setup instructions to install its dependencies and extract the required assets, then build from an MSYS2 MinGW64 shell:

```sh
make -j
```

The executable is normally produced at:

```text
build/us_pc/sm64coopdx.exe
```

Prebuilt releases may be added later, but they must not contain a ROM or copyrighted assets that the project is not permitted to redistribute.

## Using VR mode

1. Start your headset and select an active OpenXR runtime before launching the game.
2. Connect a gamepad.
3. Launch `sm64coopdx.exe`.
4. Open the game's settings and enter the **VR** menu.
5. Enable **VR Mode**.
6. Open **Camera Settings**, select **Third Person Mode**, and adjust **Camera Distance (%)** to your preference.

The game can also be configured to launch in VR automatically. If VR initialization fails, launch with `--console` and check the lines beginning with `[VR]` for the detected loader, runtime, headset, and session state.

## Controls and camera behavior

This milestone is intentionally a seated or standing **gamepad VR** experience. Mario is controlled with the same gamepad bindings used by regular SM64 Co-Op DX, while the headset controls the player's view.

Both supported camera systems are valid choices:

- **Default camera:** works with the original SM64-style camera behavior.
- **Free camera:** works with SM64 Co-Op DX's free-camera behavior.

Neither camera mode is required or specifically recommended; use whichever you prefer.

## Current limitations

- Windows/OpenGL is the only implemented OpenXR graphics path.
- Third Person Mode is the only playable VR camera mode currently exposed.
- Motion controllers, tracked hands, hand interaction, and haptics are not implemented yet.
- First-person rendering and head-directed movement are not implemented yet.
- Some flat billboard objects, such as original 2D trees, may make their camera-facing rotation more noticeable in VR. True 3D replacement models can improve this in the future.
- Hardware and runtime compatibility testing is still limited.
- General Lua mods, model packs, texture packs, and ROM hacks require broader compatibility testing in VR.

Please include the headset, OpenXR runtime, GPU, reproduction steps, and the relevant `[VR]` console output when reporting a VR-specific bug.

## Planned direction

The long-term goal is to expand this foundation without sacrificing SM64 Co-Op DX's multiplayer and mod support. Planned work includes:

- First-person VR mode
- Head-facing movement direction for first-person play
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

The current third-person milestone is considered feature-complete for now. Development will continue from this stable checkpoint toward the first-person and tracked-hand systems.

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
