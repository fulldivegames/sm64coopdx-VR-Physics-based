# Changelog

## v0.5.14 - 2026-08-14

- Fixed Quest system keyboard activation for settings text fields, including custom palette preset naming.
- Restored the bundled Mario, Luigi, Toad, Wario, and Waluigi palette presets without overwriting existing user palette files.

## v0.5.13 - 2026-08-14

- Added the native Quest system keyboard to standalone settings text fields, including palette preset naming.
- Improved physical King Bob-omb grab reach without changing regular Bob-omb behavior.
- Improved compatibility for valid renamed or repackaged DynOS actor binaries.
- Moved Quest-specific mod and DynOS installation instructions directly below first-time setup.

## v0.5.11 - 2026-08-14

- Reduced general actor CPU cost by removing unnecessary square roots from the shared nearest-player search used by Bob-ombs, Goombas, Chain Chomp, bosses, flames, and many mod actors; targeting and behavior thresholds are unchanged.
- Removed an additional per-frame square root from Bob-omb Buddy proximity steering.
- Added a Quest startup shader warmup using combinations learned from successful gameplay, including level-dialog rendering. Newly encountered game or DynOS shader combinations remain cached for later launches.
- Made floating VR hands use matching hand display lists from the active DynOS character actor when available, preserving the pack's hand geometry, textures, lighting, and colors with a safe built-in fallback.
- Retained external mod and DynOS paths: `/sdcard/SM64VR/mods/` and `/sdcard/SM64VR/dynos/packs/`.

## v0.5.10 - 2026-08-14

- Quest standalone now creates and scans `/sdcard/SM64VR/dynos/packs/` for externally installable DynOS packs.
- Added standalone DynOS pack installation instructions.

## v0.5.9 - 2026-08-14

- Heavy rear-grab enemies such as King Bob-omb and Chuckya can now be physically grabbed with either hand.
- Rear-position validation remains required, and dive pickup remains supported.
- Added Flame & Lava Optimizations in Performance (on by default on Quest), reducing dense transparent flame rendering, visual trail particles, lava bubbles, and avoidable distance work without changing hazard collisions.

## v0.5.8 - 2026-08-14

- Added two-hand physical pickup support for heavy rear-grab enemies, including King Bob-omb and Chuckya; native dive pickups remain supported.
- Prevented cap-holding hands from triggering punches, punch sounds, motion dives, climbing, or grabbing.
- Reduced intermittent stalls with faster shader lookup, buffered shader-cache learning, and chunked Quest audio transfers.
- Retained the existing Chain Chomp squared-distance hot-path optimization.

## v0.5.7 - 2026-08-14

- Added `/sdcard/SM64VR/mods/` as the primary Quest mod directory so SideQuest can transfer large mods without entering Android's restricted `Android/data` tree.
- Added a one-time Android all-files-access settings prompt and automatic shared mod-folder creation.
- Retained the previous private mod directory as a compatibility fallback; shared mods take priority when names overlap.
- Updated the standalone mod installation and ADB instructions for the new directory.

## v0.5.6 - 2026-08-14

- Added **Shaking Hat Gives Wing Cap**, reusable physical cap removal, throwing, recovery, head reattachment, grounded/water fade timing, and rising thrown Wing Caps.
- Added configurable body opacity and smooth adjustable transparency while looking down, without fading the tracked gloves.
- Reworked physical Bowser ownership for one or two hands. The final grip release uses averaged physical hand-swing direction while preserving native Bowser throw behavior.
- Kept the v0.5.5 save/settings compatibility, Quest performance defaults, climbing, immersion, and mod-loading behavior unchanged.
- Audited the shared VR hot paths for allocation and stale-state regressions and rebuilt the ARM64 APK successfully.

## v0.5.5 - 2026-08-13

- Added an optional opaque FPS counter beneath the lives display that follows HUD corner spread.
- Added reusable physical cap throwing, re-grabbing, head reattachment, safe held fading, and the disabled-by-default **Grab Cap at Any Time** option.
- Made physical Bowser releases use tracked hand-swing direction.
- Added default-on body hiding for native ledge grabs, hangs, climb-downs, and pull-ups.
- Added default-hidden body presentation during top-of-pole flips.
- Retained the v0.5.1 save/settings persistence fixes and audited new state lifetimes for stale references.

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
