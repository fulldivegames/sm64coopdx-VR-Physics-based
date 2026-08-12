# PC version follow-up notes

Features and fixes developed in the Quest standalone branch that should also
be evaluated and ported to the PC VR build:

- Default VR presentation to VSync off and uncapped frame rate. Preserve an
  existing player's explicit display settings during migration.
- Add the VR brightness range and camera-menu brightness control.
- Add HUD corner spread while keeping each counter's icon, `x`, and number
  grouped together.
- Add scrolling and focused-item tracking to long SM64 Co-Op DX option menus.
- Allow surface swimming to exit the water by looking upward past the chosen
  threshold while pressing forward.
- Re-test Chain Chomp performance in stereo VR. The standalone branch avoids
  unconditional chain-solver square roots; profile the PC renderer before
  deciding whether a separate GPU-side optimization is necessary.
- Fix the intermittent first-person locomotion ground launch: after repeated
  forward-to-back skid/side-flip transitions, Mario can occasionally zip
  rapidly across the ground. Clamping `forwardVel` alone does not prevent it;
  audit the horizontal velocity vector and stale braking/deceleration/turning
  action transitions as well.
## Physical climbing parity

- Native overhead hangables (including the underside of Bob-omb Battlefield's first bridge) need the expanded 240-unit vertical hand-accessibility fallback when `mario->ceil` is already a `SURFACE_HANGABLE`. Keep this fallback restricted to native hangable collision; do not classify arbitrary ceilings or the seesaw platform as native hangables.
- Bob-omb Battlefield should consistently spawn all three pit bowling balls in every mission; remove the Star 2-6 act restriction from the third `bhvPitBowlingBall` entry.

## Locomotion parity

- Forward-prime and backpedal side-flip grace are 12 and 10 gameplay simulation ticks respectively, decremented in the local Mario 30 Hz joystick update. They must never use headset/render-frame deadlines.
- When entering negative-`forwardVel` backpedal representation, derive travel yaw from the current horizontal velocity, face opposite that travel yaw, preserve the world velocity, and only then approach the requested body yaw at `0x800` per gameplay tick.
