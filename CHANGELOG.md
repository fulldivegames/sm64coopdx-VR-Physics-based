# Changelog

## v0.5.11 - 2026-08-14

- Reduced general actor CPU cost by removing unnecessary square roots from the shared nearest-player search used by Bob-ombs, Goombas, Chain Chomp, bosses, flames, and many mod actors; targeting and behavior thresholds are unchanged.
- Removed an additional per-frame square root from Bob-omb Buddy proximity steering.
- Made floating VR hands use matching hand display lists from the active DynOS character actor when available, preserving the pack's hand geometry, textures, lighting, and colors with a safe built-in fallback.
- Kept all v0.5.9 gameplay, flame/lava optimization, collision, and interaction behavior unchanged.

## v0.5.9 - 2026-08-14

- Heavy rear-grab enemies such as King Bob-omb and Chuckya can now be physically grabbed with either hand.
- Rear-position validation remains required, and dive pickup remains supported.
- Added Flame & Lava Optimizations in Performance (off by default on PC), reducing dense transparent flame rendering, visual trail particles, lava bubbles, and avoidable distance work without changing hazard collisions.

## v0.5.8 - 2026-08-14

- Added two-hand physical pickup support for heavy rear-grab enemies, including King Bob-omb and Chuckya; native dive pickups remain supported.
- Prevented cap-holding hands from triggering punches, punch sounds, motion dives, climbing, or grabbing.
- Reduced render stalls with a direct shader lookup cache and removed repeated square roots from Chain Chomp proximity checks.

## v0.5.6 - 2026-08-14

- Added **Cheats > Shaking Hat Gives Wing Cap**. With **Grab Cap at Any Time** enabled, a deliberately vigorous physical shake converts the held cap before it is placed back on Mario's head.
- Completed reusable cap behavior: either hand can remove, hold, throw, recover, or physically re-equip the cap; dropped caps begin their five-second fade only after touching geometry or the water surface.
- Made thrown shaken Wing Caps rise without gravity, retain physical throw direction, and fade out after fifteen seconds instead of snapping back onto Mario.
- Added configurable body opacity and a smooth, adjustable look-down transparency effect while keeping the tracked gloves fully opaque.
- Reworked physical Bowser ownership for one or two hands. Releasing one controller transfers a two-handed hold; releasing the final grip throws using averaged hand-swing direction while preserving native weight, spin power, mines, and networking.
- Added low-overhead PC hitch attribution and explicit runtime shader-miss timing so severe stalls can be separated between game, Lua, network, audio, shader, rendering, and OpenXR work.
- Rechecked the VR hot paths for per-frame allocation and file-I/O regressions, retained the preallocated texture/interpolation storage, and rebuilt both editions successfully.

## v0.5.5 - 2026-08-13

- Added an optional fully opaque FPS counter beneath the lives counter. It follows HUD corner spread without inheriting HUD opacity.
- Added the disabled-by-default **Immersion > Grab Cap at Any Time** option. A Grip + Trigger cap can be thrown with hand velocity, picked up again, returned to Mario's head, or safely faded while held without leaving stale hand state.
- Made physical Bowser releases use the tracked hand's world-space swing direction rather than headset facing.
- Added the default-on **Model Settings > Body Settings > Hide Body While on Ledges** option for native ledge grabs, hangs, climb-downs, and pull-ups.
- Added PC mouse-wheel input for long VR settings panels, using the actively selected control when no mouse-hover target exists.
- Added the default-hidden top-of-pole-flip body option and retained the tracked gloves during the flip.
- Audited the new controller, cap, HUD, menu-scroll, and update-checker state paths for balanced lifetimes and stale references.

- Synchronized a physically gripped moving pole's graphics transform with its live collision transform so the visible model rises and falls with the player.
- Added the default-on **Immersion > Underwater Filter**, a low-cost 25%-opacity castle-water-blue veil that appears only when the tracked headset is below the water surface.
- Enabled every visible Immersion option by default for v0.5.0, including the ledge-climb camera.
- Fixed the remaining startup crash by registering the VR twirl-effect geometry and display lists with DynOS before it creates writable runtime copies.
- Capped the forward-to-back side-flip skid opportunity at 18 gameplay ticks, preserving the tested release movement and side flip while preventing a held reverse direction from dragging Mario indefinitely.
- Anchored the twirl tornado directly below the tracked headset, kept its base at character foot height, and increased its size by 5%.
- Welded the original two-unit Castle Grounds exterior-wall corner split that becomes visible as a vertical sky crack in close stereoscopic views.
- Interpolated snow, blizzard, water-snow, flower, lava-bubble, whirlpool, and jet-stream effect camera samples at render rate while retaining the deterministic 30 Hz simulation.
- Added small headset contact for native damage and star collection, plus hand-contact star collection, while excluding Bowser keys and unrelated warps or interactions.
- Restored native forced drops when a physically holding player is damaged or knocked back, and removed old-frame interpolation from hand-held objects so they track the glove more tightly.
- Restored baby-penguin handoffs to Tuxie's mother while physically carrying, late-latched held-object rendering to controller rate, and forced tracked releases on damage.
- Added optional ledge-catch/climb camera following, raised the first-person star-door unlock star, and stabilized straight-up/down swimming yaw.
- Added Cheats sliders for Swimming Speed (up to 300%) and Running Speed (up to 200%), both defaulting to native 100% speed.
- Made physical grips inherit moving-pole displacement and enabled the native top-of-pole flip when the headset reaches the top 3% of a pole or tree.
- Staggered interpolated snowflake integer steps to reduce synchronized visual judder without changing the 30 Hz simulation.

All notable SM64 Co-Op DX VR changes are summarized here. This project is still experimental, so compatibility and behavior may continue to change between releases.

## v0.5.0 - Testing

- Fixed the immediate Windows startup crash introduced by the twirl effect by giving its DynOS-relocated display-list commands independent storage.
- Made the successful-exit VR hat gesture reliably grabbable with either hand using a larger headset-area grab volume. The held window is now about one minute; releasing the cap drops a non-interactive copy onto level geometry, where it rests briefly and fades away.
- Added physical climbing for poles, trees, native hangable ceilings, monkey bars, and grates, including held-grip auto-catch, hand-over-hand anchoring, headset-synchronized interaction collision, safe release placement, normal dropping, and optional momentum-based swing releases.
- Added safe ledge completion after physically pulling the headset over a climb edge, local triangle-bounded wall constraints for rounding open corners, headset-centered climb pickup collision, and forward-facing hanging dismounts without an accidental experimental 180-degree turn.
- Added separate toggles for physical grabbing, physical climbing, standard grabbing, and standard climbing. Standard grabbing remains enabled by default; standard climbing is now disabled by default.
- Added the disabled-by-default **Climb Any Wall or Ceiling** cheat for ordinary vertical walls and genuinely overhead solid ceilings while continuing to exclude floors. Cheat-only contacts now require a fresh, close Grip press and reject breakable boxes, cap blocks, and other box surfaces; held-grip jump auto-catch remains limited to native poles, trees, and hangables.
- Hid the local torso and legs automatically during physical climbing while retaining the tracked gloves.
- Added Headset, Left Controller, and Right Controller movement-facing sources with calibration and tracking-loss fallback.
- Added **Turn During Jumps**, which changes Mario's facing on supported landing frames without discarding momentum, speed, timers, or jump-combo state.
- Added a HUD Corner Spread slider and selection-following scrolling for long settings panels. VR auto-scroll now chooses one absolute position only when selection changes, preventing stale text geometry from jumping outside the panel.
- Stabilized VR menu highlighting by retaining one immutable DJUI layout per gameplay tick instead of alternating a second mid-tick menu rebuild.
- Kept main and pause menus at full headset resolution independently from gameplay Render Scale.
- Added a VR Brightness slider.
- Added an installed-version and update-status display to the main menu, including a GitHub release link when a newer version is available.
- Added the Immersion submenu with smooth crouch/sand camera motion, a face-stuck blackout, cannon aim-direction bounds/guidance, and head-tracked horizontal 3D positional audio.
- Added an immediate white painting-entry comfort fade capped at 1.5 seconds.
- Improved first-person water exits by accepting forward input while looking at least 45 degrees upward near the surface.
- Preserved native Crazy/Jumping Box carry behavior while keeping ordinary physically held enemies decoupled from Mario's forced pickup movement.
- Stabilized True First Person so acrobatic pitch/roll does not unexpectedly change the player's horizontal heading.
- Reverted first-person ground locomotion to the proven v0.4.0 release behavior after the experimental Movement Overhaul caused inconsistent directional transitions. Headset/controller-relative steering, reverse jogging, native skid/turnaround momentum, side flips, and Side-Flip Camera Follow remain available; climbing, motion controls, camera, immersion, and later jump features are unaffected.
- Aligned first-person view forward with level/area-loading door spawns and made Mario's body follow headset yaw exactly while genuinely standing still.
- Replaced nearest-face hangable detection with a direct scan for native `SURFACE_HANGABLE` triangles across static, moving, floor, ceiling, and wall collision partitions. Native grabs now require the glove's swept path to touch the actual underside plane and Mario to remain below it; standing on top of BOB's bridge can no longer pull the player through a broad ceiling-reference volume. Ordinary ceilings remain unavailable unless the cheat is enabled.
- Stabilized physical climbing by consuming controller displacement in a translation-free tracking frame, keeping the native gameplay anchor fixed during a hold, and transferring that stable anchor between hands. Camera movement can no longer feed back into the next controller sample and shake the world. Added stick-down sliding while gripping real poles or trees.
- Added hand-touch collection for normal, Wing, Metal, and Vanish Caps.
- Added one compact 32-radius, 64-height headset-centered object-interaction collider while swimming, actively flying, and physically climbing. Its original radius, height, and offset are restored afterward and its position returns to Mario's current body position, while Mario's environment/physics collider remains on the native gameplay body for stability.
- Added grip-plus-trigger peace-sign gloves during star/key collection sequences.
- Added a successful-exit-only cap gesture: during a star/Bowser course exit, bring either Grip-and-Trigger-held glove into the generous grab region covering the headset and space just above it. The chosen hand can hold the cap for roughly one minute. Releasing it drops a visual-only, non-interactive cap onto level geometry; it rests briefly and fades. The cap takes priority over the overlapping victory peace-sign input. Death and Exit Course paths cannot activate it.
- Added **Cheats > Flying Speed (%)**, a 100%-to-300% non-compounding Wing Cap velocity multiplier. Its default remains the original 100% flight speed.
- Audited Koopa and shell source placement; their original act-specific level entries and behavior files remain unchanged.
- Reduced repeated OpenXR pose-state calls, controller-to-world transforms, billboard work, object-hit work, shader lookup, shader-cache file I/O, and per-object 3D-audio listener setup.
- Reserved graph-node interpolation records once per level, avoiding repeated heap allocations as new actors enter view and reducing a source of intermittent VR frame-time spikes.
- Added the new SM64 Co-Op DX VR logo to the game and project page.
- Added the default-on **Effects > Twirl Tornado Effect**: Shy Guy, Spindrift, and Tweester twirl states now place a collisionless rotating white tornado shell at the character's feet for the complete twirl. The effect is scaled to roughly 75% of the selected character and rendered at 25% transparency.
- Added **Model Settings > Body Settings > Feet Only (Hide Torso and Legs)**, disabled by default, and moved mounted-action body visibility into Body Settings.
- Reorganized Side-Flip Camera Follow, 180 Degree Wall-Jump Camera Turn, and Physical Crouching / Ground Pounds into Immersion. Renamed True Diving to **True Diving (Camera Effect)** and added dedicated Body, Hand, and Effects settings pages.
- Expanded the face-stuck blackout beyond the nominal HUD rectangle so snow and sand face-plants cover the complete per-eye field while the Mario-font message remains a normally fused stereo HUD element.
- Kept the twirl effect local, non-networked, non-interactive, and allocation-free after its one action-entry spawn; it is reused for the full action and removed immediately afterward.

This version remains in local testing and has not been published as a GitHub release yet.

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
