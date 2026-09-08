# Native macOS port, work in progress

**The ARM64 port passes the current short-match matrix covering 29 selectable
VS stages, 26 character entries and 35 common item kinds. It remains incomplete.**
The latest results and per-case logs are in `dist/macos/validation/REPORT.md`.
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
