# SM64 Co-Op DX VR Standalone v0.6.0

v0.6.0 is a major optimization and mod-compatibility release for the physics-based Quest standalone edition.

## Highlights

- Large renderer batching and redundant state-change reductions, including the distant Chain Chomp/Bob-omb Battlefield performance path.
- General actor, flame, lava, weather/effect, menu, shader-preparation, and startup optimizations.
- 72, 90, and 120 Hz refresh-rate selection, with 120 Hz selected by default on both Quest 2 and Quest 3.
- Correct HUD opacity across normal HUD elements while keeping the optional FPS counter readable.
- Character Selector and multiple character packs specifically tested with a stereoscopic in-headset selection overlay.
- Shared `/sdcard/SM64VR/` paths for Lua mods, DynOS packs, custom palettes, and learned shader-cache data.
- Safer Quest text-entry teardown, fixing the server-player-count crash caused by closing an active field with B.
- Continued stability improvements across first person, third person, physical carrying, climbing, caps, Bowser throws, and mod-heavy startup.

The README contains the complete SideQuest installation guide, standalone mod paths, default controls, physical-interaction tutorial, third-person mode, settings, compatibility notes, and troubleshooting.

The APK contains no ROM or Nintendo game assets. Select your own legally obtained, unmodified Super Mario 64 US ROM on first launch. The app may close after importing it; reopen the app from Unknown Sources.
