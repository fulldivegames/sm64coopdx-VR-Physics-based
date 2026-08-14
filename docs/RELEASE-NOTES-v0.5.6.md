# SM64 Co-Op DX VR v0.5.6

This maintenance release completes the reusable physical-cap feature, improves physical Bowser throws, and adds targeted diagnostics for rare PC VR stalls.

## Highlights

- Remove, hold, throw, recover, and physically place Mario's cap back on his head when **Immersion > Grab Cap at Any Time** is enabled.
- Enable **Cheats > Shaking Hat Gives Wing Cap** and shake the removed cap vigorously to convert it. Putting it back on grants the Wing Cap; throwing it sends it rising away before it fades.
- Adjust body opacity and the smooth look-down transparency comfort effect without fading the tracked gloves.
- Grab Bowser with one or two hands. A two-handed hold continues when one hand releases, and the final release aims the native throw from the physical controller swing.
- Severe PC stalls now report which frame stage was responsible; slow runtime shader misses are identified separately.

## Windows installation

1. Download `SM64-Co-Op-DX-VR-Windows-v0.5.6.zip`.
2. Extract the complete archive to a new folder. Do not run the executable from inside the ZIP.
3. Keep every included DLL beside `SM64-Co-Op-DX-VR.exe`.
4. Start the game, provide your own legally obtained unmodified Super Mario 64 US ROM when requested, and enable VR from **Settings > VR**.

The archive contains no ROM or Nintendo game assets.

## Diagnosing a severe PC hitch

Launch `SM64-Co-Op-DX-VR.exe --console`. A stall of at least 150 ms reports timings for network, interpolation, game, Lua, audio, and render/OpenXR work. A slow first-use shader compilation produces a separate `[GFX] Runtime shader cache miss` line.
