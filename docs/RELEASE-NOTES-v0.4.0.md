# SM64 Co-Op DX VR v0.4.0

Version 0.4.0 focuses on camera stability, cannon play, physical object handling, desktop capture, and performance. It includes all features from v0.3.1 and earlier.

## Highlights

- Headset-directed cannon aiming.
- Automatic horizontal view alignment when cannon aiming begins.
- Original cannon firing limits with a gradual comfort fade outside the valid cone.
- Mario-style arrows that point back toward the nearest valid cannon direction.
- First-person camera isolation from tunnels, doors, conversations, Bowser introductions, and other scripted camera pulls.
- Experimental True Diving for players who want the camera to follow Mario's animated dive pose.
- Improved Mips and supported physical carry/drop behavior.
- Clean first-/third-person switching with third-person recentering and state cleanup.
- More stable smooth turning, skyboxes, billboards, lighting, and camera roll.
- A complete, uncropped left-eye desktop mirror for recording and streaming.
- Reduced CPU work in motion-controller polling, physical punches, billboard processing, world-space hand conversion, and shader lookup.

## Install

1. Download `SM64-Co-Op-DX-VR-Windows-v0.4.0.zip` from this release.
2. Extract the entire ZIP to a normal folder. Do not run the game from inside the ZIP.
3. Start the headset software and select the OpenXR runtime you want to use.
4. Run `SM64-Co-Op-DX-VR.exe`.
5. When prompted on first launch, drag your own legally obtained, unmodified Super Mario 64 US `.z64` ROM onto the game window.
6. Open **Settings > VR**, enable **VR Mode**, and select a camera under **Camera Settings**.

The ZIP contains no ROM or Nintendo game assets. The executable is not code-signed, so Windows SmartScreen may ask for confirmation. Windows Firewall may also ask about network access because SM64 Co-Op DX contains online multiplayer.

## Default VR controls

| Action | Default input |
| --- | --- |
| Move | Left stick |
| Smooth turn / camera | Right stick |
| Jump / fire cannon | Right Primary |
| Attack / interact | Right Secondary |
| Crouch | Left Trigger |
| L button | Left stick click |
| R button | Right stick click |
| Pause | Left Menu |
| Close fist / physical grab | Hold that hand's Grip |
| Right Trigger punch | Disabled by default; optional in Controller Settings |

Use **Settings > VR > Controller Settings** to remap the sticks and buttons. A connected gamepad remains usable alongside the VR controllers.

## How to use the motion controls

- **Punch:** Hold a grip to close that glove, then make a deliberate punch. The tracked fist is the hit point.
- **Grab and throw:** Close either grip while a glove overlaps a supported grabbable actor. Keep holding to carry it. Release gently to drop it or release during a faster hand movement to throw it.
- **Dive:** Punch with both hands at nearly the same time. This can trigger an air dive or a running ground dive when their individual settings are enabled.
- **Crouch / ground pound:** With Physical Crouching enabled, lower the headset by roughly one third of the calibrated standing height. Crouching while airborne triggers a ground pound.
- **Bowser:** Grip his tail with either hand, move the held hand in a turning arc to build spin speed, and release to throw. The camera stick can also help turn.
- **Cannon:** Enter normally, aim with the headset, and press Jump to fire. If the view darkens, follow the Mario arrow back toward the legal firing cone.

## Camera notes

- First Person Mode remains the default VR camera.
- Third Person Mode works with the original camera and SM64 Co-Op DX free camera.
- First-person walking, swimming, Wing Cap flight, shell riding, and cannon aiming use headset direction.
- Scripted camera events still run for gameplay timing, but no longer rotate or pull the normal first-person headset view.
- **Experimental > True Diving** temporarily attaches the camera to the animated dive pose. It is off by default and may cause discomfort.
- **Experimental > True First Person** and **Arms Mode** remain optional and may cause discomfort or visual issues.

## Performance and capture

- Lower **Performance > Render Scale** if GPU performance is limited.
- Disable **Performance > Desktop View** to remove spectator-mirror work entirely.
- When enabled, Desktop View shows the complete left-eye image. Black bars may appear because the headset eye and desktop window have different shapes.
- Common shaders are prepared at startup. Newly encountered shader definitions are learned in `gfx_shader_cache.txt` and prepared on later launches.

## Known limitations

- Windows/OpenGL is currently the only implemented OpenXR graphics path.
- Solo hosting has received the most testing. Online co-op, other OpenXR runtimes, and mods need broader testing.
- True First Person, True Diving, and Arms Mode are experimental.
- The 3D star models still do not appear on the Star Select screen in VR.
- Original flat billboard objects can still look unusual in stereo VR.
- Some ROM hacks, Lua mods, character models, and texture packs may have camera, interaction, or performance problems.

For diagnostics, open PowerShell in the extracted folder and run:

```powershell
.\SM64-Co-Op-DX-VR.exe --console
```

Include the headset, OpenXR runtime, GPU, reproduction steps, and relevant `[VR]` or `[GFX]` console output when reporting a problem.
