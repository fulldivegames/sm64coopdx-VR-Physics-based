# SM64 Co-Op DX VR v0.3.0

Version 0.3.0 introduces tracked motion-controller gameplay on top of the existing first- and third-person VR modes. Third Person Mode is still included, and a normal gamepad can still be used.

## Highlights

- First Person Mode is now the default VR camera mode.
- Tracked, player-colored floating gloves with controller haptics.
- Remappable VR-controller sticks and buttons, with optional gamepad use.
- Physical punching, grabbing, throwing, diving, crouching, and ground pounds.
- One-handed Bowser tail grabs with hand-driven spin momentum and physical release.
- Head-directed walking, swimming, Wing Cap flight, and turtle-shell riding.
- VR locomotion with 90-degree forward steering, a reverse jog, and a preserved side-flip window.
- Character-specific first-person camera height and improved body/camera anchoring.
- Adjustable camera, glove, body, HUD, render scale, and desktop mirror settings.
- Automatic recalibration when VR starts.
- Faster texture and shader handling, including startup shader precompilation and a learned shader cache.

## Default VR controls

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
| Close fist / grab | Hold that hand's Grip |
| Right Trigger punch | Disabled by default; can be enabled |

Use **Settings > VR > Controller Settings** to remap the controls. Primary and Secondary button names are used so the menu can work across different OpenXR controllers.

## Physical actions

- **Punch:** Hold a grip and make a deliberate punch. Your tracked glove is the hit point.
- **Grab and throw:** Close either grip near a grabbable object. Keep holding to carry it; release gently to drop it or release during a fast hand motion to throw it.
- **Dive:** Punch with both hands at nearly the same time. This can trigger an air dive or, while running fast enough, a ground dive.
- **Crouch / ground pound:** Lower the headset by about one third of your calibrated standing height. Do this in the air to ground pound.
- **Bowser:** Grab his tail with either hand, move the held hand in a turning arc to build momentum, and release the grip to throw him. The camera stick can also help turn.
- **Hoot and supported moving actors:** Grip to attach and release to let go.

Punch sensitivity, grab behavior, dive types, glove size and position, Bowser spin, body visibility, camera placement, and performance options are available in the VR submenus.

## Install

1. Download `SM64-Co-Op-DX-VR-Windows-v0.3.0.zip` below.
2. Extract the whole ZIP to a normal folder.
3. Start the OpenXR runtime for your headset.
4. Run `SM64-Co-Op-DX-VR.exe`.
5. When asked, drag your own legally obtained, unmodified Super Mario 64 US `.z64` ROM onto the game window.
6. Open **Settings > VR** and enable **VR Mode**.

The ZIP does not contain a ROM or Nintendo game assets. The included build is unsigned, so Windows SmartScreen may ask for confirmation. Windows Firewall may also ask about network access because the game contains online multiplayer; only allow the networks you intend to use.

## Important notes

- This is still an early community VR project.
- Windows/OpenGL is currently the only OpenXR graphics path.
- Solo hosting is tested. Online co-op and broad mod compatibility need more testing.
- True First Person and Arms Mode are experimental and may be uncomfortable.
- Star models are currently missing from the Star Select screen in VR.
- A newly encountered mod-specific shader may still hitch once before it is learned for later launches.

For troubleshooting, run `./SM64-Co-Op-DX-VR.exe --console` in PowerShell and include the relevant `[VR]` and `[GFX]` lines in bug reports.
