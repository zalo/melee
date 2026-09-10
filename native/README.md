# Native ports, work in progress

Both platforms are developed on `main`. Shared runtime code stays in this
directory; platform launchers and resources live in `platform/macos` and
`platform/linux`. Build, packaging and test scripts stay in `tools`.

Experimental Miyoo Flip V2 ARM64 instructions: [FLIP.md](FLIP.md).

Linux x86-64/Vulkan instructions: [LINUX.md](LINUX.md). The validation described
below is the existing macOS ARM64 result; it is not Linux or Omarchy coverage.

**The ARM64 port passes the current short-match matrix covering 29 selectable
VS stages, 26 character entries and 35 common item kinds. It remains incomplete.**
The recorded results are in [validation/2026-09-08/REPORT.md](validation/2026-09-08/REPORT.md).
See the [main README](../README.md) for current build and packaging instructions.
Earlier non-black framebuffer checks could accept a solid background; those
results are not visual acceptance. Current reports also check camera finiteness
and playfield color/edge variation after the countdown.
The recovered game code runs natively through boot, menus, character/stage
selection and matches. Keyboard input supports movement, jumping, attacks,
grabs, shielding and pause/resume. Earlier short-match tests also reached Results.
The sequential matrix checks runtime failures and numeric playfield visibility;
it does not certify every move, costume, effect, stage/fighter combination or
item behavior. Saving, adventure/target stages and other game modes remain
outside this VS acceptance pass.
