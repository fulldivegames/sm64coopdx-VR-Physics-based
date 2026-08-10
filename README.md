# SM64 Co-Op DX VR

An experimental OpenXR VR fork of [SM64 Co-Op DX](https://github.com/coop-deluxe/sm64coopdx).

Version 0.4.0 improves first-person camera stability, physical object handling, cannon play, desktop capture, and VR performance. It builds on the tracked motion controls and first- and third-person modes introduced in earlier releases. Third Person Mode remains available and works with both the default camera and SM64 Co-Op DX's free camera.

> [!IMPORTANT]
> This is an early project, not a finished VR port. It currently targets 64-bit Windows, OpenGL, and OpenXR. Solo hosting is tested; online co-op still needs broader testing. ROM hacks, Lua mods, model packs, and texture packs may work, but compatibility varies.

## Download and play on Windows

Normal players do not need Git, MSYS2, a compiler, or any command-line build tools.

1. Open the [latest release](https://github.com/fulldivegames/sm64coopdx-VR/releases/latest) and download the Windows ZIP.
2. Extract the entire ZIP to a normal folder. Do not run the game from inside the ZIP.
3. Start your headset software and make sure the OpenXR runtime you want to use is active.
4. Launch `SM64-Co-Op-DX-VR.exe`.
5. On first launch, drag your legally obtained, unmodified **Super Mario 64 US `.z64` ROM** onto the game window when prompted.
6. Open **Settings > VR**, enable **VR Mode**, and choose a mode under **Camera Settings**.

The validated ROM is remembered in the user-data folder. It should not need to be supplied again on every launch.

> [!IMPORTANT]
> The release ZIP does not contain a ROM or Nintendo game assets. You must provide your own legally obtained, unmodified US ROM. Never upload a ROM when reporting a problem.

If Windows SmartScreen appears, confirm that the ZIP came from this repository's official release page. This community build is not currently code-signed. Windows Firewall may also ask about network access because SM64 Co-Op DX includes online multiplayer; only allow the networks you intend to use.

### Player requirements

- A 64-bit Windows PC
- A PC VR headset and controllers with a working OpenXR runtime
- OpenGL support compatible with the active OpenXR runtime
- A legally obtained, unmodified Super Mario 64 US ROM
- Optional: a standard gamepad; all main actions can also be mapped to VR controllers

The current build has been tested with a Meta Quest 3 using Virtual Desktop's OpenXR runtime. Other conformant OpenXR headsets and runtimes may work, but have not all been tested.

## What is included in v0.4.0

- Native stereoscopic rendering through separate left- and right-eye OpenXR swapchains
- Full 6DoF headset tracking for looking, leaning, and positional movement
- Third-person and first-person VR camera modes
- Head-directed first-person walking, swimming, Wing Cap flight, and shell riding
- Head-directed cannon aiming with automatic entry alignment, firing-cone comfort fade, and directional Mario HUD arrows
- Smooth horizontal turning with the camera stick
- Tracked, player-colored floating gloves with hand poses and haptics
- Physical punches, grabs, throws, dives, crouching, and ground pounds
- One-handed physical Bowser tail grabbing and hand-driven spin momentum
- Remappable VR-controller sticks and buttons
- Optional first-person torso and legs, plus experimental tracked arms
- Character-specific first-person camera-height settings
- Automatic tracking recalibration when VR starts and camera/body reset on level changes
- Adjustable camera, movement, glove, body, HUD, and performance settings
- Head-locked menus, HUD, text boxes, and Star Select interface
- First-person camera isolation from tunnels, doors, conversations, and other scripted camera pulls
- Corrected stereo skybox, lighting, terrain, billboard, and culling behavior
- Stable live switching between first- and third-person modes
- Optional complete left-eye desktop mirror for recording or streaming without cropping
- Startup shader precompilation and a learned shader cache to reduce gameplay stutters
- Faster shader lookup, billboard processing, controller tracking, and physical-hit detection
- Improved physical handling for Mips and other supported carryable actors
- Flat-screen play remains available whenever VR Mode is off

## Camera and movement

### Third Person Mode

Third Person Mode keeps the character visible and supports either the original camera or SM64 Co-Op DX's free camera. Camera distance is adjustable. Neither camera system is required or specifically recommended; use whichever feels better to you.

### First Person Mode

First Person Mode is the default VR camera. The headset controls the view and Mario's forward direction, while the selected camera stick provides smooth horizontal turning.

The default VR locomotion allows normal forward movement and steering up to 90 degrees left or right without making Mario turn his back to the player. Pulling backward produces a steerable reverse jog. Moving forward and then pulling backward preserves a short skid window so a side flip can still be performed. **Experimental > Original Mario Movement** restores Mario's original movement rules.

Swimming, active Wing Cap flight, and turtle-shell riding follow the headset's look direction. Look up or down to change vertical direction while flying or swimming; the normal gameplay buttons still provide swimming strokes and other actions.

Cannons align the virtual view horizontally with the barrel when aiming begins. Move your head to aim and press the normal Jump input to fire. Your head remains free, but the shot stays inside the cannon's original firing limits. Looking outside those limits gradually darkens the view and displays a Mario-style arrow pointing back toward the usable firing cone.

First-person mode removes the local character's head and normal arms so they do not block the view. Player-colored tracked gloves remain visible. The torso and legs can be shown or hidden under **Model Settings**.

## Default VR controller layout

The exact physical button names vary between headset brands. In the menu, **Primary** usually means A/X and **Secondary** usually means B/Y.

| Action | Default input |
| --- | --- |
| Move | Left stick |
| Smooth turn / camera | Right stick |
| Jump | Right Primary |
| Attack / interact | Right Secondary |
| Crouch | Left Trigger |
| L button | Left stick click |
| R button | Right stick click |
| Pause | Left Menu |
| Form a fist / physical grab | Hold the matching hand grip |
| Button punching | Off by default; optionally use Right Trigger |

Open **Settings > VR > Controller Settings** to change either stick and every listed action. Inputs can be disabled individually. A connected gamepad remains usable alongside the VR controllers.

## Physical actions

- **Punch:** Hold a grip to close that glove, then make a deliberate punch. The tracked fist is the attack point, so Mario's body does not need to face the target. Punch speed, travel distance, grip strength, and collider length are adjustable.
- **Grab and throw:** Close either grip while that glove overlaps an object or character the game marks as grabbable, such as a Bob-omb, Mips, or a baby penguin. Keep holding to carry it at your hand. Release gently to drop it, or release during a faster hand movement to throw it.
- **Dive:** Punch forward with both hands at nearly the same time. In the air this triggers a dive; while running fast enough on the ground it triggers a running dive. Air and ground motion dives can be enabled separately.
- **Crouch and ground pound:** With **Physical Crouching / Ground Pounds** enabled, lower the headset by roughly one third of your calibrated standing height. Mario stays crouched while you remain below the threshold. Crouching while airborne triggers a ground pound.
- **Bowser:** Reach either glove to Bowser's tail and close that grip. Move the held hand in a turning arc around your body to build spin speed, then release the grip to throw. The camera stick can also help turn. Bowser spin acceleration and maximum speed are adjustable.
- **Hoot and moving actors:** Grip a supported physical interaction point to attach to it, then release the grip to let go. This uses the game's existing interaction and networking paths.

Physical punches can play Mario's one-two punch voice sequence, and supported interactions provide controller haptics. The optional **Enable Punch Button (Right Trigger)** setting restores a traditional right-trigger punch without changing grip-based physical punching.

## VR settings guide

| Submenu | What it controls |
| --- | --- |
| Camera Settings | Camera mode, third-person distance, per-character first-person height, forward/back position, movement-direction calibration, and field of view |
| Controller Settings | VR input enable, movement and camera sticks, action mappings, and optional trigger punching |
| Motion Control Settings | Physical punches and grabs, motion dives, punch sound, punch thresholds, collider length, and Bowser spin tuning |
| Model Settings | First-person torso/legs, torso and leg placement, glove size, rotation, and position |
| Performance | Headset render scale, desktop mirror on/off, and desktop mirror frame rate |
| HUD Settings | HUD opacity |
| Experimental | Side-flip camera follow, 180-degree wall-jump turns, flat first person, True First Person, True Diving, Arms Mode, body visibility during mounted actions, physical crouching, original movement, and reverse speed |

Every VR submenu includes a **Set to Defaults** button.

Lowering **Render Scale** can improve performance while still filling the headset display. Turning off **Desktop View** removes the spectator mirror and can save additional GPU work. When enabled, the desktop mirror fits the complete left-eye image into the window and may show black bars to preserve the full frame. On startup, the game prebuilds common shaders and stores newly discovered shader definitions in `gfx_shader_cache.txt`; later launches can prepare those shaders before gameplay.

## Current limitations

- Windows/OpenGL is the only implemented OpenXR graphics path.
- Motion controls are still under active development and may have action-specific edge cases.
- **True First Person**, **True Diving**, and **Arms Mode** are experimental. True First Person follows animated body motion and may cause discomfort or motion sickness. True Diving temporarily follows the animated dive pose and may also be uncomfortable.
- The 3D star models currently do not appear on the Star Select screen in VR. The screen remains usable.
- Original flat billboard objects, such as some 2D trees, can make their camera-facing rotation more noticeable in VR. True 3D replacement models can improve this.
- Some levels, actions, Lua mods, character models, texture packs, and ROM hacks may still have camera, visual, or performance problems.
- Hardware, OpenXR runtime, and online co-op compatibility testing is still limited.

When reporting a VR problem, include the headset, OpenXR runtime, GPU, reproduction steps, and the relevant `[VR]` or `[GFX]` console output. Launch from PowerShell with `./SM64-Co-Op-DX-VR.exe --console` to keep the diagnostic console visible.

## Building from source (developers only)

Players should use the release ZIP. Building is only necessary to modify the source or test unreleased changes.

### Developer requirements

- The normal [SM64 Co-Op DX build prerequisites](https://github.com/coop-deluxe/sm64coopdx)
- A Windows/MSYS2 MinGW64 build environment
- Git and GNU Make

After installing the upstream Windows build prerequisites, clone this fork, check out the `vr` branch, and build from an **MSYS2 MinGW64** shell:

```sh
git clone --branch vr https://github.com/fulldivegames/sm64coopdx-VR.git
cd sm64coopdx-VR
make -j
```

The executable is produced at `build/us_pc/sm64coopdx.exe`.

To create the allow-listed Windows player ZIP after a successful build, run this from PowerShell:

```powershell
.\tools\package-vr-windows.ps1 -Version dev
```

The ZIP is written to `dist/`. The packaging script verifies that no `.z64`, `.n64`, or `.v64` ROM was staged. Nintendo ROMs and game assets must never be committed, uploaded as workflow artifacts, or attached to a release.

## Planned direction

- Physical swimming driven by tracked swimming strokes
- Optional physical running driven by natural arm swings
- Physical Wing Cap steering using both hands
- Two-handed steering while sliding
- Hand anchoring for wall slides and wall jumps
- Hand-mounted HUD placement with size, angle, position, and distance controls
- Further arm IK work, interaction refinement, performance tuning, and compatibility testing

These are goals, not promised release dates.

## Credits and acknowledgements

- [SM64 Co-Op DX](https://github.com/coop-deluxe/sm64coopdx) and the Coop Deluxe Team for the multiplayer game and codebase this project forks
- [sm64ex-coop](https://github.com/djoslin0/sm64ex-coop) and djoslin0 for the project continued by SM64 Co-Op DX
- The Super Mario 64 decompilation and PC-port contributors whose work underpins the upstream project
- [Khronos OpenXR](https://www.khronos.org/openxr/) for the cross-platform XR API and redistributable OpenXR loader used by this project

No source code from another SM64 or Zelda VR mod has been incorporated into this release. Additional third-party work will only be bundled when its license or author explicitly permits redistribution, and it will be credited if used.

## Legal and licensing notice

This is an unofficial fan project and is not affiliated with or endorsed by Nintendo, the SM64 Co-Op DX team, Meta, Virtual Desktop, or Khronos. Super Mario and related names, characters, and assets are property of their respective owners.

This fork retains the upstream repository's history and third-party notices. The checkout does not currently contain a single project-wide license file, so do not assume that every file or bundled component is covered by one license. Each dependency and third-party component remains subject to its own terms. Do not redistribute Nintendo ROMs or game assets, and obtain permission before bundling third-party mods or model packs.

## Upstream project

SM64 Co-Op DX is an online multiplayer continuation of sm64ex-coop maintained by the Coop Deluxe Team. For upstream documentation, community information, and non-VR issues, visit the [official SM64 Co-Op DX repository](https://github.com/coop-deluxe/sm64coopdx).
