# Android/PC VR parity notes

- **Implemented in both current trees:** Effects > Twirl Tornado Effect, the
  character-scaled collisionless white twirl shell, Body/Hand model pages,
  Feet Only body rendering, mounted-body relocation, the three requested
  Immersion relocations, and the True Diving (Camera Effect) label. Keep these
  config keys and defaults identical across PC and Android.

- **Current movement baseline (supersedes every older Movement Overhaul note
  below):** PC and Android have reverted first-person ground locomotion to the
  v0.4.0 release implementation. Do not re-enable the later direction-locked
  Movement Overhaul, its independent travel-vector state, lateral skid state,
  or its config/menu option. Preserve current headset/controller-relative
  facing selection, later jump features, and momentum-derived Side-Flip Camera
  Follow. The old `vr_movement_overhaul` config key is intentionally ignored.
- Use the shared `custom_coopdx_logo.rgba32.png` asset containing the new
  matching 3D-style red/blue "VR" mark. Replace the standalone home-menu and
  loading-screen logo with it so PC, Android, release packaging, and the
  GitHub README all present the same SM64 Co-Op DX VR branding.
- Port the Quest swimming surface-exit behavior to PC: while near the surface,
  pressing jump with the movement stick forward should exit the water when the
  headset is aimed at least 45 degrees upward. This replaces the unintuitive
  backward-stick requirement in first-person VR while preserving flat play.
- Add the VR HUD Corner Spread slider from the Android port so players can
  push lives, coins, stars, health, and other HUD icons farther toward their
  respective corners.
- Add the VR Brightness slider from the Android port, including its 10% to
  120% range.
- Add selection-following vertical scrolling to Co-op DX panels so long VR
  settings menus remain fully accessible instead of clipping lower options.
- Port the PC True First Person heading lock to Android: character animations
  may contribute pitch and roll during flips, dives, and rollouts, but must not
  rotate the player's horizontal view. Intentional side-flip and wall-jump
  camera turns should remain the only action-driven yaw changes.
- Make main-menu and pause-menu rendering independent from gameplay Render
  Scale. Menus must always use 100% of the headset's recommended eye
  resolution even when gameplay returns to a lower configured scale.
- Add Camera Settings controls for Facing Direction (Headset by default, Left
  Controller, or Right Controller) and Facing Calibration (0-100, 50 centered).
  Controller modes must use the OpenXR aim-pose orientation, fall back to grip
  pose only when required, and safely fall back to the HMD if tracking is lost.
- Preserve native carry behavior for Crazy/Jumping Boxes when physically
  grabbed so their bounce sequence still carries Mario. Keep Bob-ombs,
  ordinary enemies, and other normal hand-held objects decoupled from Mario's
  forced pickup/backstep movement.
- Add the PC painting-entry comfort transition: when a VR painting warp is
  accepted, start a quick fade to white immediately and hold white until the
  destination/Act Select screen takes over, with the VR hold capped at 45
  gameplay frames (1.5 seconds at 30 Hz). Flat mode keeps the original delayed
  transition and timing.
- Port the PC physical-climb collision rewrite: while either hand remains
  anchored, center Mario's temporary interaction cylinder on the tracked HMD
  so coins and other pickups can be collected at the player's real position.
  Keep the view on the playable side of the grabbed surface, then convert back
  to a wall/floor/ceiling-resolved feet position on release so Mario cannot be
  left embedded in geometry.
- Make ordinary hangable ceilings reliable with a held grip by sweeping both
  the glove-sized contact footprint and the complete controller path between
  gameplay samples. This must catch triangle seams and a ceiling crossed
  during a jump instead of requiring a perfectly timed grip press.
- Hide Mario's torso and legs unconditionally while a physical pole, tree,
  wall, or ceiling climb is active. Keep the tracked gloves visible and restore
  the configured body immediately after a safe release.
- Default Enable Standard Climbing to off while leaving Enable Standard
  Grabbing on. The physical climbing system remains enabled by default; Reset
  to Defaults must restore this same combination.
- Add the VR Cheats submenu and its disabled-by-default "Climb Any Wall or
  Ceiling" option. When enabled, physical grips may anchor to non-intangible,
  near-vertical walls and ordinary ceiling surfaces, but never floors; normal
  mode must continue to accept only poles/trees and native hangable ceilings.
- Add the default-on Motion Control Settings option "Turn During Jumps." On
  the exact frame a normal jump, double/triple jump, side flip, backflip, long
  jump, freefall, held-object jump, or quicksand jump lands in first-person
  VR, rotate only Mario's facing and intended yaw to the HMD view direction.
  Do not alter the horizontal velocity vector, scalar speed, landing timers,
  or jump-combo counters, so momentum and chained-jump state remain intact.
- Match the PC forward-to-back side-flip reliability fix: remember a valid
  forward run for 12 gameplay frames while the thumbstick crosses neutral,
  then let walking, braking, and decelerating enter the same native skid and
  side-flip grace window. Never arm the assist for back-to-forward movement.
- Add the default-on VR Immersion submenu and match its five PC options:
  Crouch / Sand Camera, Face-Stuck Blackout, Cannon Aim Direction Cone, and
  Head-Tracked 3D Sound, plus Movement Overhaul. Reset to Defaults must enable
  all five.
- Smoothly lower the first-person camera a small amount while crouching and
  follow both native quicksand depth and the feet-, butt-, and head-stuck soft
  ground actions. These offsets are visual only and must not change Mario's
  collision or gameplay position.
- While ACT_HEAD_STUCK_IN_GROUND is active in first-person VR, cover both eyes
  with opaque black and render "Face stuck in the ground" in the game's
  colorful Mario font. Clear the overlay immediately when the action ends.
- Match the PC cannon comfort rules: headset rotation remains free, the aim is
  clamped to the native cannon yaw/pitch limits, firing is blocked while the
  headset is outside those limits, and the fade reaches black shortly beyond
  the real boundary. Show four fixed Mario-font arrows at north, south, east,
  and west, all pointing inward toward the valid firing view.
- Port the PC head-tracked positional-sound listener. In first-person VR,
  calculate object sound vectors from the HMD's world position and horizontal
  facing rather than the final stereo-eye render matrix. UI/global sounds stay
  centered, and pitch/roll must not tilt the virtual ears.
- Extend physical ceiling detection to monkey bars and grates whose collision
  triangle is exposed through the engine's floor partition. Sample a complete
  glove footprint and both triangle windings; only accept native hangables in
  normal mode. With Climb Any Wall or Ceiling enabled, an upward-facing solid
  may be treated as a ceiling only when it is genuinely overhead with enough
  playable clearance below, so the ground never becomes grabbable.
- Match the PC's explicit assisted-skid phase: arm it once on a forward-to-back
  transition, bypass native slope acceleration, clamp carried speed to a
  non-negative 24-unit maximum, rebuild forward/slide horizontal vectors every
  tick, and clear the phase after at most four gameplay frames. Enter walking
  immediately with negative speed on that same handoff. Holding Back must not
  re-arm the phase or leave Mario stationary; the longer Jump grace stays
  active so Jump can still produce the normal side flip.
- On level/area-loading door spawns, align the first-person camera yaw basis to
  the new spawn's forward yaw. While genuinely stationary (not intangible,
  swimming, climbing, or hanging), make Mario's body yaw match the configured
  HMD/controller facing source exactly without rotating stored momentum.
- Port the PC native-hangable discovery fix instead of relying on Mario's
  current ceiling: directly inspect static and dynamic floor, ceiling, and
  wall collision partitions for `SURFACE_HANGABLE` triangles under the glove
  footprint and swept controller path. Use the real glove reach plus 20 units,
  not a broad 240-unit vertical volume. Require signed hand contact at/below
  the actual triangle plane (8-unit seam tolerance), exact X/Z projection
  inside fallback triangles, and at least 24 units between Mario's feet and
  the overhead plane. Standing on top of BOB's bridge must never pull Mario
  through; from below the glove must actually touch/cross the underside.
  Keep ordinary ceilings unavailable unless Climb Any Wall or Ceiling is on.
- Consume physical-climb hand displacement once per tracked update by moving
  the saved contact point after applying its delta. This prevents accumulated
  offset drift on poles and posts. While physically gripping a real pole/tree,
  stick-down may lower the climb anchor at up to 6 world units per gameplay
  frame, clamped above the pole/floor bottom; cheat surfaces must ignore it.
- Calculate physical-climb pull deltas from a translation-free controller
  tracking vector (camera inverse rotation only), while retaining the ordinary
  world glove for surface discovery. Keep Mario's native pole/hang action
  position fixed throughout the hold and move only his interaction object to
  the headset; commit the accumulated camera offset once on release. Transfer
  this tracking-space anchor between hands so camera motion cannot feed back
  into controller position and cause bridge/monkey-bar jitter.
- Center the compact temporary object-interaction cylinder vertically on the
  HMD during physical climbing and apply it before the same tick's interaction
  scan; climb syncing must not overwrite it with an uncentered object position.
  This is required for coins and pickups below eye height to register.
- Bound the grabbed-surface safety plane to within 48 world units of the
  actual collision triangle edge. Once the HMD clears a real opening, stop
  treating the old triangle as an infinite wall so the other hand can catch
  an adjacent/inside surface. On a non-swing release, if the HMD is over a
  walkable, head-clear ledge above the previous floor, finish Mario safely on
  that ledge instead of dropping below it.
- Cheat-only wall/ceiling starts and second-hand transfers require a fresh
  Grip press and a tightly capped physical hand reach. Held-grip jump catch
  remains available only for poles, trees, and native `SURFACE_HANGABLE`
  geometry. Reject collision owned by breakable/exclamation/cap boxes,
  hidden boxes, metal boxes, and jumping boxes even when the cheat is on;
  their ordinary punch and jump interactions remain unchanged.
- When leaving a physical hang via `ACT_WALL_KICK_AIR`, do not apply the
  experimental wall-jump 180-degree camera turn. Preserve the headset-facing
  launch direction exactly as with pole dismounts.
- Match PC movement tuning: restore native left/right turnaround detection so
  lateral side flips are symmetric, use a cubic diagonal backpedal steering
  curve (about 32 degrees at a normal diagonal), and cap the assisted
  forward-to-back skid at 16 with 4 units of deceleration for at most four
  gameplay ticks before the negative-speed backpedal handoff.
- Refine that movement parity so only a genuinely forward-primed stick
  reversal enters the assisted skid. Add default-on Immersion > Movement
  Overhaul. While enabled, gradual transitions both into and out of the rear
  half must preserve current horizontal speed and the world-space travel vector,
  then steer smoothly toward the requested direction instead of braking to zero
  or rebuilding immediately from the wrapped input yaw. Use normal walking's
  32-unit maximum (24 on slow ground) and acceleration curve all around the
  stick while keeping negative scalar speed/body-facing backpedal representation.
  A slow 360-degree thumbstick circle in either direction must not stop at east
  or west, and Mario must not turn and run a circle in the rear half. Disabling
  Movement Overhaul restores the previous directional backpedal-speed blend and
  forward-recovery handoff; retain the old config key for compatibility but
  remove the visible Backpedal Speed line from Experimental.
  Every backpedal entry gets an eighteen-simulation-tick side-flip opportunity;
  count both this window and the twelve-tick forward-prime window in 30 Hz
  gameplay updates rather than headset/render frames. Restart the flip window
  after an actual skid finishes, never arm the assist during backward-to-forward
  recovery, and leave the existing side-flip/skid state and timers unchanged.
  Validate native side flips in both lateral directions plus forward-to-back
  flips before accepting parity.
- While physical climbing is active, explicitly scan for `INTERACT_COIN`
  overlap with a small HMD-centered collectible cylinder (32 radius, 64
  height). Object collision may be generated before the late HMD collider
  update, so this safe collectible-only path is required. Do not extend it to
  enemies, hazards, doors, or warps and do not permanently move Mario's
  environment collider.
- In Controls/How to Play documentation, explicitly tell players to lift the
  headset over a ledge and let go of Grip to climb up onto it.
- Pool graph-node interpolation records at level scope (PC reserves 8192 and
  falls back safely on overflow) so newly visible scene objects do not each
  allocate heap memory during headset frames. Use the equivalent Android scene
  interpolation structure rather than copying an inapplicable PC renderer.
- In VR, keep one immutable DJUI menu display list per 30 Hz gameplay tick;
  do not rebuild panel layout a second time mid-tick. Continue patching the
  stable menu projection for every OpenXR eye frame. This avoids alternating
  hover/layout states that appear as button flicker.
- Allow either tracked glove to collect normal, Wing, Metal, and Vanish Caps
  on contact by using the normal cap interaction path, including Lua hooks,
  networking-compatible state, cap timers, music, and haptics.
- During swimming, active Wing Cap flight, and physical climbing, replace
  Mario's object-interaction cylinder with one compact HMD-centered cylinder
  (32-unit radius, 64-unit height). Save and restore the original radius,
  height, and down-offset when the state ends, then return the object position
  to Mario's current `mario->pos` rather than a stale saved position. Do not
  move Mario's physics position or environment collider. Keep the climb-only
  direct coin scan at the same 32-by-64 dimensions for interaction-order safety.
- During star/key collection actions, Grip + Trigger on either controller
  should render Mario's peace-sign glove for that hand only until the action or
  buttons end. Include airborne star grabs, all star dances, the jumbo-star
  cutscene, and the exit save dialog.
- During successful course/Bowser exit actions only (`ACT_EXIT_AIRBORNE`,
  `ACT_FALLING_EXIT_AIRBORNE`, `ACT_SPECIAL_EXIT_AIRBORNE`, or
  `ACT_EXIT_LAND_SAVE_DIALOG`), Grip + Trigger on either tracked glove inside
  a broad cylinder covering the headset and the space just above it should
  pull Mario's held-cap hand model onto that controller. After activation the
  cap follows that hand for up to roughly 1800 gameplay ticks. Releasing the
  chosen hand drops a visual-only, non-interactive normal-cap object that uses
  ordinary gravity and geometry collision, rests for 150 ticks, then fades for
  30 ticks. Death and Exit Course paths must never activate it or change the
  player's real cap/power-up state. The cap display must take priority over
  the victory peace sign when both inputs
  are simultaneously eligible.
- Movement Overhaul must keep Mario's body yaw locked to the selected facing
  source and store stick-controlled travel as an independent horizontal vector.
  Use one full-speed curve across the complete stick circle and begin grounded
  movement from the existing landing-vector magnitude. Never reconcile this
  deliberate body/vector separation with `mario_set_forward_vel`. A lateral
  stick excursion over 0.35 invalidates the forward-to-back prime so circling
  cannot skid. A quick left/right reversal gets a direct side-flip request
  without displaying Mario's native turnaround animation; direct forward-to-back
  remains a separate, bounded twelve-tick assisted skid.
- Refine that separation with Mario's native movement feel: approach travel yaw
  toward intended yaw by `0x800` per gameplay tick, keep the original grounded
  speed buildup and landing over-speed, and never use travel yaw as body yaw.
  Feed Movement Overhaul one continuous 360-degree stick-relative yaw. Do not
  run it through the legacy front-arc/backpedal directional split: switching
  those coordinate systems at east/west makes analog noise alternate the
  desired direction and violently line-lock lateral travel.
  Convert horizontal velocity back to travel yaw with SM64's established
  `atan2s(velocityZ, velocityX)` ordering, never X then Z. Reversing those
  arguments introduces a false quarter-turn and makes steering fight its own
  vector every tick. Custom reversal skids use `CHAR_ANIM_SKID_ON_GROUND`,
  preserve their vector on exit, and never display a turnaround animation.
  A direct left/right reversal owns a twelve-tick vector-scaling skid and
  sixteen-tick jump request. Normal circular input clears the reversal prime
  and never skids. The forward-to-back skid also lasts twelve ticks and may
  retain speed up to 48 before its bounded deceleration.
- Add the persisted Cheats slider `Flying Speed (%)`, range 100-300 and default
  100. Scale the resulting VR flight velocity without compounding Mario's
  stored forward speed every gameplay tick.
- Make long-menu auto-scroll selection-driven and absolute. Do not repeatedly
  add corrections from stale selected-text geometry each render; the selected
  text and label must stay attached to its control.
