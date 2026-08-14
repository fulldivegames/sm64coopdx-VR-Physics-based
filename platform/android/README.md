# SM64 Co-Op DX VR for Quest v0.5.9

Player-facing installation, controls, gestures, features, and troubleshooting
are documented in the repository's main `README.md`. The public release is a
single APK designed for SideQuest installation.

The standalone build has been tested on Quest 3 only. Quest 2 has not been
tested at all, and both the standalone and PC editions remain incompletely
tested across the entire game and third-party content.

## Third-party software

The Android build compiles Lua 5.3.5 from source. Lua is Copyright (C)
1994-2018 Lua.org, PUC-Rio, and is used under the MIT-style Lua license included
in `third_party/lua-5.3.5/doc/readme.html`.

## Supplying the ROM

The APK never includes a Super Mario 64 ROM or Nintendo game assets. Each
player must provide their own legally obtained, unmodified US ROM.

On a fresh installation, launch the app and use Android's file picker to select
the ROM from the Quest's Downloads folder. The app verifies this SHA-1 before
copying the file into its private storage:

```text
9bef1128717f958171a4afac3ed78ee2bb4e86ce
```

The source file can have any name in the picker. Invalid and unsupported ROMs
are rejected.

### Manual development fallback

Developers may instead copy the ROM to:

```text
/sdcard/Android/data/com.fulldivegames.sm64coopdxvr/files/baserom.us.z64
```

For example, with ADB installed and the Quest connected:

```powershell
adb push "C:\path\to\your\baserom.us.z64" "/sdcard/Android/data/com.fulldivegames.sm64coopdxvr/files/baserom.us.z64"
```

Do not commit the ROM, place it in a release archive, or upload it to GitHub.

## Current standalone build

- Quest 3 NativeActivity lifecycle
- OpenXR stereo session and per-eye swapchains
- OpenGL ES rendering
- US ROM discovery and validation
- First-launch Android document picker
- Full SM64 Co-Op DX game loop and menus running natively on ARM64
- First-person VR locomotion, tracked gloves, physical interactions, and climbing
- Quest performance levels, foveation, render scale, and learned shader cache
- Shared `/sdcard/SM64VR/mods/` loading with the legacy private mod directory retained as a fallback

## Physical climbing

Close a grip while the matching glove touches a native pole, tree, or hangable
surface, then pull or move your hands. To finish a ledge climb, lift or move
your headset over the ledge and release the grip to climb up onto it.

The optional climb-any-surface cheat still requires a fresh grip close to real
wall or ceiling collision. It does not grab empty air, and boxes remain punchable
rather than becoming climb anchors.

## First-person locomotion timing

Ground locomotion is restored to the v0.4.0 release baseline while retaining
the current headset/controller-relative facing source and later jump features.

## v0.5.6 test checklist

- Install over v0.5.0 without clearing app data. Confirm existing stars and VR
  settings load, collect another star, force-stop and reopen the app, and confirm
  both progress and settings persist.
- Confirm the main menu reports v0.5.6 and the update checker accepts the
  `v0.5.6` standalone release tag.
- Confirm FPS, physical-cap, Bowser-release, and ledge-body settings behave as documented.

### Retained v0.5.0 coverage

- Compare first-person ground movement directly with v0.4.0: facing-relative
  steering, reverse jogging, native skids/turnarounds, and acceleration.
- Run forward, pull backward, and press Jump during the skid. Confirm the
  release-style side flip still works and Side-Flip Camera Follow turns toward
  the jump's actual momentum.
- Enter the direct forward-to-back skid with excess landing/long-jump speed and
  confirm legitimate momentum up to 48 is retained instead of being cut to 16.
- Land from a long jump while holding a new travel direction. Confirm landing
  momentum is preserved even though Mario's body immediately relocks to the
  selected facing source.
- Slowly rotate the stick across both east/west points without stopping. Then
  deliberately reverse left-to-right and right-to-left inside the lateral band;
  each deliberate reversal must retain the native turnaround and side flip.
- In Cheats, test Flying Speed at 100%, 200%, and 300% during headset-directed
  Wing Cap flight. Speed must scale from the native velocity without compounding
  every frame, and Set to Defaults must restore 100%.
- From above BOB's first bridge, gripping must not pull Mario through it. From
  below, the glove must touch or cross the actual underside to attach.
- While climbing, swimming, and actively flying with the Wing Cap, verify the
  compact headset interaction cylinder without changing physics, hazards, or
  warps.
- On a successful exit only, bring either glove to the broad headset/above-head
  grab region and hold that hand's Grip + Trigger. The cap must appear instead
  of the victory peace sign and follow either selected hand. Release it and
  verify that it falls onto geometry, rests briefly, and fades. Death and Exit
  Course must never activate it.
- Navigate long VR settings pages. Text must not jump between frames; automatic
  scrolling must move once to keep the newly selected row visible.
- Lift your headset over a ledge and let go of Grip to climb onto it.
