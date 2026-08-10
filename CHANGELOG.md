# Changelog

All notable SM64 Co-Op DX VR changes are summarized here. This project is still experimental, so compatibility and behavior may continue to change between releases.

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
