# Changelog

## v0.5.1 - 2026-08-12

- Fixed Android virtual-filesystem mounting when the app-data directory contains dots, restoring saved stars and settings after relaunch.
- Automatically recovers and synchronizes existing v0.5.0 EEPROM save copies, so progress already written to storage can load again.
- Updated the standalone version and release-tag handling so v0.5.0 installations can report v0.5.1 as available.

- Synchronized moving-pole graphics with their live collision transform during physical grips.
- Added the default-on 25%-opacity underwater filter and enabled all Immersion defaults.
- Deepened Quest-only saturation and contrast slightly without changing the Brightness setting.

- Fixed the remaining startup crash by registering the VR twirl-effect geometry and display lists with DynOS before it creates writable runtime copies.
- Capped the forward-to-back side-flip skid opportunity at 18 gameplay ticks, preserving the tested release movement and side flip while preventing a held reverse direction from dragging Mario indefinitely.
- Anchored the twirl tornado directly below the tracked headset, kept its base at character foot height, and increased its size by 5%.
- Welded the original two-unit Castle Grounds exterior-wall corner split that becomes visible as a vertical sky crack in close stereoscopic views.
- Interpolated snow, blizzard, water-snow, flower, lava-bubble, whirlpool, and jet-stream effect camera samples at render rate while retaining the deterministic 30 Hz simulation.
- Added small headset contact for native damage and star collection, plus hand-contact star collection, while excluding Bowser keys and unrelated warps or interactions.
- Restored native forced drops when a physically holding player is damaged or knocked back, and removed old-frame interpolation from hand-held objects so they track the glove more tightly.

All notable SM64 Co-Op DX VR changes are summarized here. This project is still experimental, so compatibility and behavior may continue to change between releases.

## v0.5.0 - Testing

- Fixed the immediate Windows startup crash introduced by the twirl effect by giving its DynOS-relocated display-list commands independent storage.
- Made the successful-exit VR hat gesture reliably grabbable with either hand using a larger headset-area grab volume. The held window is now about one minute; releasing the cap drops a non-interactive copy onto level geometry, where it rests briefly and fades away.
- Added the default-on **Effects > Twirl Tornado Effect**, a local collisionless rotating white tornado shell for the complete Shy Guy, Spindrift, and Tweester twirl state.
- Added a disabled-by-default feet-only first-person body option and separate Body and Hand model pages.
- Moved mounted-action body visibility into Body Settings and moved side-flip follow, wall-jump turning, and physical crouching into Immersion.
- Renamed True Diving to **True Diving (Camera Effect)**.
- Expanded the face-stuck blackout to the complete per-eye field while keeping its Mario-font message on the normal fused stereo HUD layer.
- Restored baby-penguin handoffs while physically carrying, forced damage drops, and render-rate hand-held object tracking.
- Added optional ledge-climb camera following, a raised star-door unlock presentation, and stable vertical swimming direction.
- Added Swimming Speed (100%-300%) and Running Speed (100%-200%) cheat sliders.
- Added moving-pole physical-climb displacement and top-of-pole flip detection from headset height.
- Smoothed snowflake integer stepping without changing gameplay simulation.

## v0.4.0 - 2026-08-10

- Added headset-directed cannon aiming.
- Added one-time cannon entry alignment, native firing-cone limits, a gradual comfort fade, and Mario-style directional guidance arrows.
- Prevented tunnels, doors, conversations, Bowser introductions, and other scripted cameras from pulling the first-person VR view.
- Added experimental True Diving, which follows Mario's animated dive position only while diving.
- Improved physical carry/drop behavior for Mips and other supported held actors.
- Fixed live camera-mode switching so third person resets cleanly, recenters on Mario, and does not retain first-person hands or camera state.
- Stabilized first-person skyboxes, billboards, lighting, camera roll, and smooth-turn interpolation.
- Changed the desktop mirror to show the complete left-eye frame without cropping.
- Reduced per-frame work in controller tracking, physical punch sweeps, controller-to-world transforms, billboard processing, and shader lookup.

See [the v0.4.0 release notes](docs/RELEASE-NOTES-v0.4.0.md) for installation, controls, and testing notes.

## v0.3.1

- Stabilized first-person pole and tree climbing.
- Made pole and tree dismounts launch in the headset-facing direction.
- Added investigation safeguards around a reported long-session level-entry crash.

## v0.3.0

- Added tracked motion-controller gameplay, physical punches, grabs, throws, dives, crouching, ground pounds, and Bowser interactions.
- Made First Person Mode the default VR camera and added head-directed movement for major mounted and swimming actions.
- Added controller remapping, character camera profiles, startup shader preparation, and learned shader caching.
- Added VR performance controls, including render scale and optional desktop-view disabling.

See [the v0.3.0 release notes](docs/RELEASE-NOTES-v0.3.0.md) for the full original summary.
