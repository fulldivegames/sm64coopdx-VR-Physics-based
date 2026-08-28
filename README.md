<p align="center">
  <img src="textures/segment2/custom_coopdx_logo.rgba32.png" alt="SM64 Co-Op DX VR" width="720">
</p>

<p align="center">
  <strong>PC VR edition</strong> &nbsp;|&nbsp;
  <a href="https://github.com/fulldivegames/sm64coopdx-VR-Standalone-Physics-based"><strong>Quest standalone edition</strong></a>
</p>

<h1 align="center">sm64coopdx-VR (Physics-based)</h1>

<p align="center">
  A native OpenXR VR fork of <a href="https://github.com/coop-deluxe/sm64coopdx">SM64 Co-Op DX</a> with first-person motion controls and an optional third-person gamepad mode.
</p>

> [!IMPORTANT]
> The current public release is **v0.8.2 — Updater and Release Notes Update**. This is an active fan project: neither the PC nor Quest standalone edition has been tested across every level, headset, multiplayer situation, ROM hack, or mod combination. Occasional crashes or issues might occur.

SM64 Co-Op DX VR currently targets 64-bit Windows, OpenGL, and OpenXR. It keeps the original game's flat-screen mode and multiplayer foundation while adding native stereoscopic rendering, 6DoF tracking, VR-aware cameras, remappable motion-controller input, physical interactions, comfort options, and extensive calibration settings.

Solo hosting has received the most testing. Online co-op, different OpenXR runtimes, ROM hacks, Lua mods, character models, and texture packs may work, but compatibility still varies.

### Multiplayer and public-lobby safety

This is an unofficial, unauthorized fan build and is not endorsed by the SM64 Co-Op DX or Nintendo teams. **Public VR Lobbies** are intended for this project's PC VR and Quest standalone builds. **Regular Public Lobbies** use the compatible public directory; all VR cheats and power-ups/special moves are disabled there. Play regular public lobbies at your own risk and follow the server rules. Private lobbies and direct connections remain available for invited cross-platform sessions.

VR nametags are disabled by default in v0.7.7 because their world-space placement still needs additional work. They can be enabled manually in the multiplayer HUD settings for testing.

### Voice chat

Compatible PC VR and Quest standalone clients include built-in voice chat for Public VR Lobbies and supported private/direct sessions. It also works for flat-screen players using this PC build. Quest uses the headset microphone; PC players can choose from the microphones recognized by Windows.

Open **Server Settings > Voice Chat** to enable or disable voice chat, mute your microphone, adjust microphone level, select a PC microphone, or adjust other-player volume. The **Players** menu supports individual two-way mutes and shows a speaker icon beside players who are actively speaking. The on-screen keyboard's microphone button can also dictate speech into the active text field. This project's voice traffic is not sent in Regular Public Lobbies.

## Download and install on Windows

Normal players do **not** need Git, MSYS2, a compiler, or build commands.

### What you need

- A 64-bit Windows PC
- A PC VR headset and tracked controllers
- An active OpenXR runtime compatible with OpenGL
- A legally obtained, unmodified **Super Mario 64 US `.z64` ROM**
- Optional: a standard gamepad for Third Person Mode or mixed input

The current PC build has been tested most often on a Meta Quest 3 through Virtual Desktop's OpenXR runtime. Other conformant PC OpenXR headsets and runtimes may work but have not all been tested.

### First-time setup

1. Open the [latest verified release](https://github.com/fulldivegames/sm64coopdx-VR-Physics-based/releases/latest).
2. Under **Assets**, download the Windows ZIP named similar to `SM64-Co-Op-DX-VR-Windows-vX.Y.Z.zip`.
3. Right-click the downloaded ZIP, select **Extract All**, and extract the complete archive to a normal writable folder. Do not run the game from inside the ZIP.
4. Start the software used by your headset, then make sure the OpenXR runtime you intend to use is active. For example, select Virtual Desktop's OpenXR runtime before launching the game if you use Virtual Desktop.
5. Run `SM64-Co-Op-DX-VR.exe` from the extracted folder.
6. On first launch, drag your own legally obtained, unmodified Super Mario 64 US `.z64` ROM onto the game window when prompted.
7. From the main menu, open **Settings > VR** and enable **VR Mode**.
8. Open **Camera Settings** and choose **First Person Mode** or **Third Person Mode**.
9. Stand in a comfortable neutral position and use **Recalibrate Tracking** if your initial facing or height is wrong.

The validated ROM is remembered in the game's user-data folder, so it normally does not need to be supplied again after an update. The release ZIP includes the OpenXR loader and required MinGW runtime libraries.

> [!WARNING]
> The release ZIP contains no ROM and no Nintendo game assets. Never upload a ROM with a bug report or redistribute one with this project.

## Install mods on Windows

The Windows release supports compatible SM64 Co-Op DX mods from the `mods` folder beside the game executable.

1. Download a mod made for SM64 Co-Op DX. The community browser is [mods.sm64coopdx.com](https://mods.sm64coopdx.com/mods/).
2. Extract the archive. Do not place the `.zip` itself in the game folder.
3. Copy the extracted mod folder into `mods` inside your extracted SM64 Co-Op DX VR folder. Its files should be directly inside that one folder, without an extra duplicate nesting level.
4. Fully close and reopen the game. From the main menu, open **Host > Mods**, select the installed mod, and start the game. Use **Refresh** if the mod was copied while the game was open.

DynOS packs use a separate folder. Extract the complete pack folder into:

```text
dynos/packs/My Pack Name/
```

Restart the game, then enable the pack from the DynOS menu. Install one mod or pack at a time when troubleshooting; desktop mods can still conflict with VR rendering or depend on a different SM64 Co-Op DX version.

### Updating an existing installation

1. Fully close the game.
2. Run **Co-op DX VR Updater.exe** from the current game folder.
3. The updater checks the latest PCVR GitHub release with a Windows ZIP, installs it, and launches the updated game. Its console closes after the game starts.
4. Recheck **Settings > VR** because new releases may add settings or change defaults.
5. Keep the previous folder until the new version has been tested with your headset and saves.

The updater preserves the validated ROM and normal user configuration stored in the game's user-data folder. If the updater cannot find a Windows asset or reports an error, keep the current installation and retry later.

## Install mods on Windows

The Windows release keeps the normal SM64 Co-Op DX mod layout inside the extracted game folder:

```text
SM64-Co-Op-DX-VR-Windows-v0.8.2/
├── mods/
├── dynos/
│   └── packs/
└── palettes/
```

1. Download a mod made for SM64 Co-Op DX from a source you trust, such as the [community mod browser](https://mods.sm64coopdx.com/mods/).
2. Extract the download. Do not leave the mod as a ZIP unless its own instructions explicitly require that.
3. Put Lua/gameplay mods in `mods/<Mod Name>/`.
4. Put DynOS character/model packs in `dynos/packs/<Pack Name>/`.
5. Put compatible palette files in `palettes/`.
6. Restart the game. Enable gameplay mods from **Host > Mods** and DynOS packs from the DynOS menu.

Avoid duplicate nesting such as `mods/My Mod/My Mod/files`. Install one mod or pack at a time when troubleshooting. Mods that replace camera, player animation, HUD, rendering, input, or character geometry can conflict with VR features even when they work in normal SM64 Co-Op DX.

## Tutorial: controls and how to play

### Default PC VR controls

Physical button names vary by controller. **Primary** normally means A/X or the main trackpad/select click; **Secondary** normally means B/Y or the secondary/menu click.

| Action | Default input |
| --- | --- |
| Move | Left stick or left trackpad |
| Smooth turn / camera | Right stick or right trackpad |
| Jump / cannon fire | Right Primary |
| Attack / interact | Right Secondary |
| Crouch | Left Trigger |
| L button | Left stick/trackpad click |
| R button | Right stick/trackpad click |
| Pause | Left Menu |
| Close fist / physical grab | Hold the matching hand Grip/Squeeze |
| Button punch | Disabled by default; optionally Right Trigger |
| Optional trigger jump | Disabled by default; optionally Right Trigger |

Open **Settings > VR > Controller Settings** to select either stick for movement/camera and remap every listed action. Individual bindings can be disabled. A connected gamepad remains usable alongside the VR controllers.

The PC build supplies native OpenXR bindings for Meta Touch, Valve Index, HTC Vive wands, Windows Mixed Reality/Samsung Odyssey, and Khronos simple controllers. It also supports the optional generic-controller profile so SteamVR and other OpenXR runtimes can map additional controllers. Runtime bindings can still vary, so use SteamVR's binding interface or the in-game remapping menu if a physical label differs.

### First-person mode and movement

First Person Mode is the default VR camera. The headset controls the view, and the configured facing source determines Mario's forward direction. The selected camera stick provides smooth horizontal turning without enabling vertical stick camera movement.

The default VR locomotion locks Mario's body toward the selected headset or controller facing direction while the stick independently controls his world-space travel direction. Forward, lateral, and backward movement share Mario's normal acceleration and over-speed curve across the complete 360-degree stick circle, while travel direction retains the original gradual steering inertia. Landing and long-jump velocity remain the starting momentum for grounded movement. A deliberate left/right reversal produces a short skid and side-flip window, while a direct forward-to-back flick retains its separate skid window. Traveling around the stick normally cancels both reversal gestures, so an ordinary circle cannot trigger a skid.

The locomotion transition code validates both Mario's scalar speed and horizontal velocity vectors, preventing repeated skid/side-flip transitions from retaining a stale sign or launching Mario across the ground. **Experimental > Original Mario Movement** restores the unmodified movement rules.

With **Turn During Jumps** enabled, Mario turns toward the configured facing direction on the exact frame a supported jump lands. Momentum, speed, landing timers, and jump-combo state are preserved, allowing chained jumps to be redirected without erasing their normal physics.

Swimming, active Wing Cap flight, and turtle-shell riding follow the direction you look. Looking up or down changes vertical swimming or flight direction, while the normal action button still performs swimming strokes. Near the water surface, looking at least 45 degrees upward and pressing forward uses a more natural first-person water exit.

When Mario is actively swimming, flying, or shell riding, his first-person body can be hidden to avoid camera clipping. Painting entries immediately fade to white and remain covered for up to 1.5 seconds or until the destination/Act Select screen takes over, preventing views through unloaded castle geometry.

### Physical actions

### Punching

Hold a hand grip to close that glove, then make a deliberate punch. The tracked fist is the attack point, so Mario's body does not need to face the target. Punch speed, required travel distance, grip strength, and collider length are adjustable. Supported hits provide haptics and can play Mario's one-two punch voice sequence.

The optional **Enable Punch Button (Right Trigger)** restores a conventional button punch. It is off by default so triggers do not unintentionally form fists.

### Grabbing, carrying, and throwing

Close either grip while a glove overlaps a supported grabbable object or character, including Bob-ombs, Mips, baby penguins, and other actors accepted by the game's normal interaction rules. The object moves to the tracked hand. Release gently to drop it, or release during a faster hand movement to throw it.

**Enable Standard Grabbing** separately controls Mario's original punch/dive pickup behavior and is on by default. Special objects such as Crazy/Jumping Boxes retain their native carry-and-bounce behavior instead of being treated like ordinary hand-held enemies.

### Motion-controlled dives

Punch both hands forward within the configured timing window. While airborne this can trigger a jump dive; while running fast enough on the ground it can trigger a ground dive. Jump and ground motion dives can be enabled independently.

### Physical crouching and ground pounds

With **Physical Crouching / Ground Pounds** enabled, lowering the headset below roughly two-thirds of the calibrated standing height holds Mario's crouch input. Crossing the same threshold while airborne triggers a ground pound. The camera eases downward while crouching instead of snapping.

### Bowser

Reach either tracked glove to Bowser's tail and hold that grip. Move the held hand around your body to build spin speed, then release to throw in the physical hand-swing direction. One physical turning swing supplies useful initial power, and the normal camera stick can still help turn. Spin acceleration and maximum speed are adjustable.

### Hoot and supported moving actors

Grip a supported interaction point to attach to it, then release the grip to let go. The implementation keeps the game's existing action and networking paths wherever possible.

### Physical climbing

Physical climbing is enabled by default. **Enable Standard Climbing** is disabled by default so touching a pole or hangable ceiling does not automatically take control away from the player's hands.

### Poles and trees

Reach a glove to a pole or tree and hold its grip. That hand becomes the anchor, allowing the player's view and body to remain physically offset from the center of the object. Grab with the other hand to change anchors. Releasing both hands drops Mario; pressing Jump dismounts in the configured facing direction with normal momentum.

### Monkey bars and hangable ceilings

Hold a grip before or while jumping into a native hangable ceiling, grate, or monkey-bar surface. The game sweeps the tracked hand path and glove footprint so a fast jump or triangle seam is less likely to miss. Once attached, hands become the camera anchor instead of sliding the player through the original canned hanging movement. Hand-over-hand movement is supported.

Mario's compact temporary object-interaction collider follows the tracked headset while climbing, allowing coins and supported pickups at the player's physical position to register without moving Mario's environment/physics collider. To climb onto a ledge, lift your headset over the ledge and let go of Grip; Mario finishes on top when a safe floor is available. The view is kept on the playable side of the grabbed surface. On other releases, the collider is resolved back to a safe floor/wall/ceiling position to reduce the chance of leaving Mario embedded in geometry.

The torso and legs are hidden automatically during physical climbing; tracked gloves stay visible. **Swing Off While Releasing** converts a sufficiently fast release into a headset-directed jump, while a normal two-hand release simply drops Mario.

**Model Settings > Body Settings > Hide Body While on Ledges** is enabled by default. It hides the local torso, legs, and feet throughout native ledge hanging, climbing down, and pull-up animations to prevent the first-person camera from clipping into Mario. Disable it if you prefer to see the body during those actions.

### Climb Any Wall or Ceiling cheat

The disabled-by-default **Cheats > Climb Any Wall or Ceiling** option extends physical gripping to ordinary solid vertical walls and overhead ceiling surfaces. Floors remain excluded. This is a cheat and may bypass intended level routes or encounter unusual modded collision. **Flying Speed (%)** ranges from the original 100% speed to 300% and defaults to 100%.

### Third-person mode

Third Person Mode centers the stereoscopic VR view on Mario and is intended for conventional gamepad play. It works with both the original SM64 Co-Op DX camera and the built-in free camera. Camera distance is adjustable, Mario remains the visual focus, and first-person tracked-hand/body state is cleared when switching modes.

### True First Person and True Diving

**True First Person** is an experimental option that follows animated body height and action motion. During flips, dives, and rollouts, animation pitch and roll can affect the view while horizontal heading remains stable. Ordinary running remains upright rather than inheriting every small model tilt.

**True Diving** applies the animated dive attachment only during a dive. Both options can cause discomfort or motion sickness and are disabled by default.

## Power-ups and special moves

### Fire Flower

Fire Flower is enabled by default under **VR > Special Moves**. Supported coin/1-Up item boxes have a 50% flower chance and supported cork boxes have a 30% chance. While powered up, close the right glove with Grip, hold Right Trigger to charge an orange fireball, then physically swing and release Trigger to throw it. The normal Attack button fires a quick shot outside first-person mode. Fire Mario's palette and music end with the power-up; **Cheats > Spawn Menu** can create a Fire Flower and **No Fire Flower Timer** keeps the power until a room transition.

### Rasengan and Rasen-Shuriken

**Rasengan / Rasen-Shuriken** is enabled by default under **Special Moves**. Hold Right Trigger with an open right hand and circle a closed, empty left glove around it until the Rasengan finishes charging. Keep holding Right Trigger to retain it, then touch a supported enemy or breakable box with the sphere. The optional **Rasengan Grip + Trigger** setting replaces the circling gesture.

With a charged Rasengan held, hold the mapped **Special** button—Left Secondary by default—while keeping the hand above the headset when **Rasen-Shuriken Overhead Charge** is enabled. Once charged, physically swing and release Right Trigger to throw it. It flies until it strikes geometry or a supported enemy, then expands into a rotating area-damage sphere for 1.5 seconds and fades over 0.5 seconds. Entering the sphere causes one Bob-omb-strength hit; remaining inside for its complete lifetime causes a second. King Bob-omb and Whomps accept these moves only through their back weak spots, preserve their native multi-hit boss rules, and retain normal grab behavior. Charge-time sliders are available in **Cheats**.

### Level Select and Spawn Menu

Open **Settings > VR > Cheats** while in a game:

- **Level Select** opens a scrollable list of the castle areas, all main courses, secret stages, cap stages, Bowser stages/arenas, and the ending. Courses with missions enter through their normal Act Select path.
- **Spawn Menu** creates a Wing Cap, Vanish Cap, Metal Cap, or Fire Flower above Mario. Close the menu to let the selected pickup fall into the level.

These are cheats and can bypass normal progression. They do not alter save data unless normal gameplay inside the destination does so.

### Custom color-palette selector

Open the player palette editor and choose **Color Palette** to open the controller-friendly quick selector. It provides 44 distinct named colors, including direct red/blue values, neutrals, skin tones, metallic colors, and extended crayon-style shades. Selecting a color returns to the editor and writes its RGB/hex value into the currently selected character part; Back exits without changing it. Normal RGB sliders and hex editing remain available.

Custom presets remain saved in the normal palette folder. The generated **Fireflower** preset controls the temporary Fire Flower outfit, so players can edit that preset to customize their powered-up colors; the previously selected normal palette is restored when the power ends.

## Core VR features

- Native stereoscopic OpenXR rendering with separate runtime-sized eye swapchains
- Full 6DoF headset rotation and positional tracking
- Live switching between First Person Mode, Third Person Mode, and normal flat play
- Tracked, player-colored gloves and optional body presentation
- Physical punching, grabbing, throwing, diving, crouching, climbing, cap handling, swimming, flight, shell riding, and cannon aiming
- Head-locked VR menus, pause screens, text boxes, Star Select, character-selection overlays, and HUD elements
- Head-tracked 3D audio, comfort effects, performance controls, shader preparation, and a complete left-eye desktop mirror

## Immersion and comfort

The **Immersion** submenu contains these default-on comfort and camera options:

- **Crouch / Sand Camera:** smoothly follows crouching, quicksand depth, and soft-ground actions without changing gameplay collision.
- **Face-Stuck Blackout:** covers the headset view while Mario's face is stuck in the ground and displays `Face stuck in the ground` using the game's Mario-style font until he escapes.
- **Cannon Aim Direction Cone:** keeps headset movement free but clamps cannon aim to the original yaw/pitch limits. Firing is blocked while the view is outside the usable cone, the image fades toward black, and four inward-facing Mario-style arrows point back toward the valid region.
- **Head-Tracked 3D Sound:** calculates world-object sound direction from the first-person HMD position and horizontal facing instead of the desktop camera. UI/global sounds remain centered, and headset pitch/roll do not tilt the virtual ears.
- **Camera on Body During Climb Up:** follows Mario's native ledge-catch and pull-up height instead of leaving the view floating above the animation.
- **Underwater Filter:** places a light 25%-opacity castle-water-blue tint over both eyes only while the tracked headset is below the water surface.
- **Side-Flip Camera Follow:** follows the direction of side-flip momentum.
- **180 Degree Wall-Jump Camera Turn:** turns the view with supported wall jumps.
- **Physical Crouching / Ground Pounds:** maps real crouching below the calibrated threshold to crouching, or to a ground pound while airborne.

## Twirl tornado effect

The default-on **Effects > Twirl Tornado Effect** surrounds the selected character with a small, rotating white version of the in-game tornado whenever Mario is in a Shy Guy, Spindrift, or Tweester twirl. It remains centered at the character's feet for the full twirl, scales to roughly 75% of the selected character, and is rendered at 25% transparency. It is visual only and has no collision, damage, wind, or multiplayer authority.

## Cannons

1. Enter a cannon normally.
2. When aiming begins, the virtual heading aligns horizontally with the cannon barrel.
3. Move your head to aim.
4. Press the normal Jump input to fire.

The headset remains free inside the cannon. With the default Aim Direction Cone enabled, the shot stays within the cannon's original legal limits. If you look outside those limits, follow the four inward arrows toward the center until the view clears; the cannon will not fire while outside its usable bounds.

## VR settings reference

| Submenu | Main controls |
| --- | --- |
| Camera Settings | Camera mode, third-person distance, character-specific first-person height, forward/back placement, previous Mario body height, headset/left-hand/right-hand facing source, facing calibration, FOV, and brightness |
| Controller Settings | Motion-controller input, movement/camera stick selection, action mappings, optional trigger punching, and optional Right Trigger Jump |
| Motion Control Settings | Physical punches, physical grabbing, physical climbing, standard grabbing/climbing, swing release, motion dives, jump turning, punch thresholds, collider length, and Bowser tuning |
| Model Settings | Body and Hand pages; torso/legs, optional feet-only view, crawl, ledge, pole-flip, and mounted-action visibility, body placement, glove size, rotation, and position |
| Performance | Headset Render Scale, FPS counter, Desktop View, mirror frame rate, fog control, Flame & Lava Optimizations, and Ultra Performance Mode |
| HUD Settings | HUD opacity and corner spread |
| Immersion | Crouch/sand camera, face-stuck blackout, cannon cone, 3D sound, ledge camera, underwater filter, side-flip follow, wall-jump turn, and physical crouching |
| Effects | Twirl Tornado Effect |
| Cheats | Level Select, Spawn Menu, Climb Any Wall or Ceiling, special-move charge times, and flying/swimming/running speed controls |
| Experimental | Flat first person, True First Person, True Diving (Camera Effect), Arms Mode, and original Mario movement |

Every VR submenu includes **Set to Defaults**.

## Performance and recording

- Open **Performance** and lower **Render Scale** if the GPU cannot maintain the headset refresh rate. This scales 3D gameplay while menus remain at the headset's full recommended resolution.
- Disable **Desktop View** to remove spectator-mirror presentation work entirely.
- If Desktop View is enabled, lower **Desktop Mirror FPS** to reduce capture-window overhead.
- The desktop mirror fits the complete left-eye frame into the window. Black bars may appear because headset-eye and desktop-window aspect ratios differ.
- The game prebuilds common shaders at startup. Newly encountered shader definitions are kept in `gfx_shader_cache.txt` and prepared on later launches.
- OpenXR's `xrWaitFrame` drives VR frame pacing; desktop VSync is bypassed while VR is active to avoid a second mismatched wait.

This project keeps Super Mario 64's original 30 Hz gameplay simulation and renders interpolated headset frames at the OpenXR runtime's cadence. Raising the simulation rate globally would change physics, animation timing, multiplayer behavior, and mod assumptions, so high-refresh VR smoothness is handled through render interpolation and late headset/controller poses instead.

## Mod compatibility

v0.6.27 expands compatibility around mod discovery, Lua HUD/menu presentation, character models, DynOS packs, palettes, controller profiles, and renderer state changes. Character Selector was specifically tested with multiple character packs, including its in-game selection overlay. Standard extracted mods, multiple DynOS packs, and custom palette files use the paths documented above.

Compatibility is not universal. Mods that replace camera, player action, input, HUD, rendering, collision, or character geometry can conflict with VR behavior. Install one at a time when troubleshooting and report the exact mod name/version, headset/runtime, and reproduction steps. A mod working in normal SM64 Co-Op DX does not guarantee that every first-person physical interaction will behave correctly.

When **WiddlePets** is enabled, Server Settings adds a **Pets** button that opens the mod's own `/wpets` pet-selection menu. Pet choice, spawning, and following behavior remain controlled by WiddlePets itself.

## v0.8.2 — Updater and Release Notes Update

- Added the PCVR updater executable, which finds the newest PCVR GitHub release with a Windows ZIP, installs it, relaunches the game, and closes its console after launch
- Added title-screen Release Notes pages sourced from the PCVR repository
- Fixed the updater's false failure report after a successful install

## v0.8.0 — Sonic Shoes Update

- Added the timed **Sonic Shoes** power-up, including the fitted shoe model, spawn-menu entry, balanced item-box/cork-box pool, Green Hill Zone music, and close-tracking sparkle trail
- Added the 0–300 Sonic speed slider with the established 3× baseline at zero, scaling normal movement, acceleration, turning, slopes, dives, and dive recoveries together
- Added high-speed water running while Sonic Shoes are active, with stable surface alignment and a return to swimming below the required speed or after a bonk
- Improved WiddlePets compatibility with an accessible pet-selection menu and an in-game hint showing where to find it
- Added Hammer Suit knockback against Bully-type enemies and 1.5× knockback from special-move explosions
- Included conservative caching and frame-work reductions for large mod maps without reducing visual quality or changing standard gameplay timing

## Previous Release Notes

### v0.7.9 — Voice Chat and Filters Update

- Added built-in voice chat for compatible PC VR and Quest clients, including PC flat-screen play, selectable PC microphones, Quest headset-mic support, microphone and player-volume controls, and a master voice-chat toggle
- Added individual two-way player muting, an active-speaker icon in the Players menu, and microphone dictation for the controller-operated keyboard
- Added the approved **Virtual Boy** and **Game Boy** full-scene filters with improved actor visibility while leaving normal rendering unchanged
- Added a conditional **Pets** shortcut to Server Settings when WiddlePets is active; it opens the mod's real pet-selection and following menu
- Hardened Rasengan and Rasen-Shuriken model lookup for ROM hacks such as B3313 without changing their charge, combat, physics, or visual behavior

### v0.7.2 — Boss Combat, Spawn Tools, and Camera Fixes

- Added **Cheats > Spawn Menu** with Wing Cap, Vanish Cap, Metal Cap, and Fire Flower buttons while retaining the complete scrollable Level Select
- Expanded Rasengan and Rasen-Shuriken interactions across giant Goombas, Pokeys, breakable boxes, Whomps, King Whomp, and King Bob-omb while preserving protected actors and native boss progression
- Added the persistent Rasen-Shuriken explosion volume: one hit on entry, a second hit only after continuous full-duration exposure, a 1.5-second hold, and a 0.5-second fade
- Tightened King Bob-omb's physical grab and special-move activation to a back-sized weak spot without changing ordinary Bob-ombs or other normal grabbables; impact visuals still wrap his complete model
- Added rear Fire Flower damage to King Bob-omb as one native boss hit and stabilized Whomp reactions so attacks cannot leave them walking face-down or trapped in an invalid action
- Raised and stabilized the first-person camera/body relationship, matched moving torso height to idle height, and kept the previous-height compatibility option
- Changed **Look Toward Spawned Star** to default off for new and upgraded installs and immediately clears any residual camera blend when disabled
- Raised cork-box Fire Flower odds to 30%, retained 50% supported item-box odds, and kept the smaller pickup presentation
- Added the 44-color quick palette selector with reliable controller scrolling, direct RGB/hex application, persistent custom presets, and an editable **Fireflower** power-up palette
- Kept headset/player damage contact synchronized, narrowed physical anchoring to intended actors, and slightly extended the default punch reach without limiting its slider

### v0.7.1 — Special Moves and Level Select Update

- Added the default-on **Rasengan / Rasen-Shuriken** special move with physical charging, headset-rate animation, hand contact attacks, physical throwing, collision-triggered area damage, protected-boss rules, and configurable charge controls
- Added **Cheats > Level Select**, a scrollable in-game list covering the castle, all 15 main courses, Bowser stages and arenas, secret/cap stages, and the ending; normal courses use their native Act Select entry path
- Kept the corrected first-person Mario body relationship as the default and added **Camera Settings > Use Previous Mario Body Height** for players who prefer the earlier height
- Improved tracked hand/held-effect interpolation at 72, 90, and 120 Hz while preserving the original 30 Hz gameplay simulation
- Added and refined color-palette selection, DynOS refresh, modded HUD placement, ghost button-punch presentation, Fire Flower behavior, enemy interactions, and VR camera stability

### v0.7.0 — Chat and Fireball Update

- Added a controller-friendly in-game Chat HUD with retained recent messages, an anchored on-screen keyboard, Enter-to-send behavior, and reliable B/Back navigation
- Added a **Players** HUD directly below Chat, showing connected player names, locations, and ping; press B to return
- Improved controller keyboard navigation and repeat behavior while preserving normal physical-keyboard input on PC
- Added a microphone button as a preview of future speech-to-text support; microphone transcription currently does not work and is still being developed
- Added the native VR **Fire Flower** special move, including item-box and cork-box pickups, temporary Fire Mario colors and music, tracked right-hand charging, physical fireball throws, quick button shots, enemy impacts, and settings/cheats for music, spawning, and timer behavior
- Fire Flowers are a VR-build-specific feature. They are not advertised or transferred as a public CoopNet mod and are disabled for CoopNet compatibility; use matching builds through Direct Connection for supported multiplayer testing
- Expanded fireball collision coverage, including Chain Chomp and Mr. Blizzard. Mr. Blizzard explodes, disappears, and respawns after 30 seconds
- Replaced the VR Act Select 3D stars with readable overlay stars, revealing completed missions and the next available mission in sequence
- Continued high-resolution presentation fixes for the keyboard, main-menu logo, update text, Chat, and VR menu overlays

### v0.6.27 — Co-op and Keyboards Update

- Added the shared controller-operated on-screen keyboard to editable SM64 Co-Op DX fields while preserving normal desktop keyboard input
- Improved high-resolution presentation for the keyboard, main-menu logo, and version/update status text
- Kept the PC public CoopNet directory and normal direct/private multiplayer behavior unchanged
- Maintained network-version parity with the standalone build for compatible Direct Connection play

### v0.6.0 — Optimization and Mod Compatibility Update

- Major renderer batching and texture-state reductions, including the distant Chain Chomp/BOB performance path
- General actor, flame, lava, snow/effect, menu, shader-preparation, and startup optimizations
- Correct HUD opacity across coins, stars, keys, timer, camera status, and the power meter while preserving the optional opaque FPS counter
- Character Selector and multi-character-pack compatibility work with stereoscopic menu presentation
- Expanded Lua mod, DynOS pack, palette, character-model, and shader-cache compatibility paths
- Native OpenXR binding suggestions for Meta Touch, Valve Index, HTC Vive, Windows Mixed Reality/Samsung Odyssey, and simple/generic controllers through runtimes including SteamVR
- Continued stability work across first person, third person, physical carrying, climbing, caps, Bowser throws, menus, and mod-heavy startup

## Troubleshooting

### VR Mode will not start

1. Confirm your headset is connected and awake.
2. Confirm the intended PC headset software is running.
3. Confirm an OpenXR runtime is selected and active.
4. Make sure `libopenxr_loader.dll` is beside `SM64-Co-Op-DX-VR.exe`; do not copy only the executable out of the release folder.
5. Try disabling overlays or tools that replace the OpenGL/OpenXR runtime.
6. Run the game with the diagnostic console and copy the relevant `[VR]` lines into a bug report.

### Launch with diagnostic output

Open PowerShell in the extracted game folder and run:

```powershell
.\SM64-Co-Op-DX-VR.exe --console
```

When reporting a VR issue, include:

- Headset model
- OpenXR runtime (Virtual Desktop, SteamVR, Meta Quest Link, etc.)
- GPU and driver version
- Camera mode and relevant VR settings
- Whether the issue also happens with mods disabled
- Exact reproduction steps
- Relevant `[VR]`, `[GFX]`, or crash-console output

### Windows warnings

This community build is not currently code-signed, so Windows SmartScreen may ask for confirmation. Verify that the ZIP came from this repository's official release page. Windows Firewall may also ask about network access because SM64 Co-Op DX contains online multiplayer; allow only the networks you intend to use.

VPNs can prevent CoopNet from connecting. If public or private CoopNet immediately reports **Lost connection to CoopNet**, temporarily disconnect your VPN or exclude `SM64-Co-Op-DX-VR.exe` through the VPN's split-tunneling settings, then try again.

## Current limitations

- Windows/OpenGL is the only completed OpenXR graphics path.
- Motion controls and physical climbing are still under active development and may have action- or level-specific edge cases.
- **True First Person**, **True Diving**, and **Arms Mode** are experimental and may cause discomfort or visual issues.
- The 3D star models currently do not appear on the Star Select screen in VR, although the screen remains usable.
- Original flat billboard objects can make their camera-facing behavior more noticeable in stereo VR. True 3D replacement models can improve this.
- Online co-op and alternate OpenXR runtimes need broader testing.
- ROM hacks, Lua mods, character models, model packs, and texture packs may have camera, collision, interaction, or performance problems.
- The standalone Meta Quest/Android port is a separate work in progress and is not included in the Windows release.

## Building from source (developers only)

Players should use the release ZIP. Build from source only when modifying or testing the project.

### Developer requirements

- The normal [SM64 Co-Op DX build prerequisites](https://github.com/coop-deluxe/sm64coopdx)
- A Windows MSYS2 MinGW64 environment
- Git and GNU Make
- Your own legally obtained Super Mario 64 US ROM/assets prepared according to the upstream project instructions

### Windows build

Open an **MSYS2 MinGW64** terminal, then run:

```sh
git clone --branch vr https://github.com/fulldivegames/sm64coopdx-VR-Physics-based.git
cd sm64coopdx-VR
make -j
```

The executable is normally created at:

```text
build/us_pc/sm64coopdx.exe
```

After a successful build, the repository's allow-listed packaging script can create a Windows player ZIP from PowerShell:

```powershell
.\tools\package-vr-windows.ps1 -Version dev
```

The ZIP is written to `dist/`. The script includes only the executable, required runtime libraries, player files, and license notices. It stops if a `.z64`, `.n64`, or `.v64` ROM is found in staging.

Nintendo ROMs and game assets must never be committed, uploaded as workflow artifacts, or attached to a release.

## Planned direction

- Physical swimming driven by tracked swimming strokes
- Optional physical running driven by natural arm swings
- Physical Wing Cap steering using both hands
- Two-handed steering while sliding
- Hand anchoring for wall slides and wall jumps
- Hand-mounted HUD placement with size, angle, position, and distance controls
- Further arm IK work, interaction refinement, performance tuning, and compatibility testing

These are development goals, not promised release dates.

## Credits and acknowledgements

- [SM64 Co-Op DX](https://github.com/coop-deluxe/sm64coopdx) and the Coop Deluxe Team for the multiplayer game and codebase this project forks
- [sm64ex-coop](https://github.com/djoslin0/sm64ex-coop) and djoslin0 for the project continued by SM64 Co-Op DX
- The Super Mario 64 decompilation and PC-port contributors whose work underpins the upstream project
- [Khronos OpenXR](https://www.khronos.org/openxr/) for the cross-platform XR API and redistributable OpenXR loader used by this project

No source code from another SM64 or Zelda VR mod has been incorporated into this release. Additional third-party work will only be bundled when its license or author explicitly permits redistribution, and it will be credited if used.

## Legal and licensing notice

This is an unofficial fan project and is not affiliated with or endorsed by Nintendo, the SM64 Co-Op DX team, Meta, Virtual Desktop, Steam, Valve, or Khronos. Super Mario and related names, characters, and assets are property of their respective owners.

This fork retains the upstream repository's history and third-party notices. The checkout does not currently contain a single project-wide license file, so do not assume every file or bundled component is covered by one license. Each dependency and third-party component remains subject to its own terms. Do not redistribute Nintendo ROMs or game assets, and obtain permission before bundling third-party mods or model packs.

## Upstream project

SM64 Co-Op DX is an online multiplayer continuation of sm64ex-coop maintained by the Coop Deluxe Team. For upstream documentation, community information, and non-VR issues, visit the [official SM64 Co-Op DX repository](https://github.com/coop-deluxe/sm64coopdx).
