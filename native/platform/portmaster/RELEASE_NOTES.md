# Super Smash Bros. Melee (native) for PortMaster, {{TAG}}

Native AArch64 build of *Super Smash Bros. Melee* (US 1.02) for PortMaster CFWs, built from
[zalo/melee `{{SHORT}}`](https://github.com/zalo/melee/commit/{{COMMIT}}) on {{DATE}}. It contains
no game data: you need your own dump of the disc (US, version 1.02, `GALE01`).

## Changes in this build

- **Fixes the crash when opening Special Melee.** Entering the Special Melee menu crashed the game on
  every device: that menu has more options than a fixed-size list the menu renderer used, so it wrote past
  the end and corrupted the stack (the extra safety checks in this build turned that into an immediate
  crash). The list is now sized correctly.
- **Fixes the out-of-memory crash on heavy stages (e.g. Onett) on 1 GB handhelds.** The texture cache
  kept far more GPU texture memory resident than its budget implied, growing the working set until the
  system ran out of memory and killed the game (worst on RAM-limited devices like the Miyoo Flip and the
  RG35XX family). The cache now scales to the device's RAM and releases unused textures much sooner, which
  also **greatly improves frame rate** on those stages (a match on Onett went from stuttering and crashing
  to a steady ~35 fps in testing). Tunable with `MELEE_TEXTURE_CACHE_MB` and `MELEE_TEXOBJ_IDLE_FRAMES`.
- **No more flashing textures or black stage floors on Mali GPUs.** On some Mali drivers (notably the
  Mali-G31 in the RG35XX / H700 handhelds) stage geometry and HUD icons would briefly flash the wrong
  texture, and the stage floor could turn solid black. The renderer now identifies textures by their
  contents instead of by a memory address that the game reuses across different textures, so cached
  textures no longer get crossed. This also stops a slow memory leak that could eventually kill the game.
- **Items with hitboxes strike fighters again.** Ray-gun bullets, the Super Mushroom and every other
  script-hitbox item passed straight through fighters instead of hitting them; a byte-order bug that
  dropped the "this hitbox can hit a fighter" flag on little-endian CPUs is fixed.
- **Game & Watch's sausages obey gravity.** Chef's sausages flew flat instead of arcing, because a
  64-bit pointer in the shared item state overwrote the sausage's type index; the layout is fixed so the
  sausages fall as they should.
- **More reliable GPU driver-bug detection.** On Mali GPUs whose driver needs a per-draw workaround, the
  renderer now keeps checking for the rendering bug when a heavier scene loads later (for example Fountain of
  Dreams reached after the menu), instead of only during the first few seconds — so the workaround engages on
  those scenes too. Correct drivers are unaffected.
- **Crash fix on match exit.** Leaving a match (for example exiting training mode) could crash the game
  with a segmentation fault; a stale fighter reference in the per-frame state check is now guarded.
- **Runs on more CPUs.** The build no longer uses the optional ARMv8 crypto instructions, which some
  aarch64 chips (e.g. the Raspberry Pi 4 / Cortex-A72) do not have; those devices previously crashed on
  launch with an illegal instruction. Verified now booting and rendering on Raspberry Pi 4 (V3D).
- **Internal render resolution.** On a high-resolution display (1080p or larger) the 3-D scene now renders
  at quarter resolution and is upscaled on output, which greatly relieves fill/present-bound GPUs at high
  panel resolutions (on a Pi 4 at 1080p a match went from ~10 to ~30 fps); the HUD/output stay full
  resolution and low-resolution handhelds are unaffected. Override with `MELEE_FLIP_RENDER_SCALE`
  (0 auto, 1 native, 2 half, 4 quarter).
- **Opt-in draw-call merge** for stage geometry via `MELEE_FLIP_RESIDENT_RECORDS=1` (off by default; ~+13%
  on Fountain of Dreams on a Mali-G31).

## Install

1. Download `melee.zip` below. Do not unpack it.
2. Copy it to `ports/PortMaster/autoinstall/` on the card the CFW uses for ports (`roms/ports` on
   AmberELEC, ArkOS and ROCKNIX; `ROMS/ports` on muOS; the `ports` share on Knulli and Batocera), then
   start PortMaster once: it installs the zip and removes it from `autoinstall`. Unpacking the zip
   straight into `ports/` works too.
3. Copy your disc image (`.iso`, `.gcm`, `.ciso` or `.rvz`, see the
   [Dolphin ripping guide](https://wiki.dolphin-emu.org/index.php?title=Ripping_Games)) into
   `ports/melee/assets/`.
4. Refresh the game list, start **Super Smash Bros. Melee (native)** from Ports, and answer **Yes**
   at the memory-card prompt on first boot. Saves live in `ports/melee/runtime/config`.

The first matches after installing stutter while shaders compile; later runs use the pipeline cache
in `ports/melee/runtime/cache`. Full controls, port settings and the developer menu are in the port's
`README.md` (inside the zip; PortMaster shows it from the port's info screen).

| Handheld | Game |
| --- | --- |
| Left stick | Control stick |
| D-pad | D-pad (taunt) |
| Right stick | C-stick |
| A / B | A / B |
| X, Y | Jump |
| R1 | Z (grab) |
| L2 / R2 | L / R (shield) |
| Start | Start |
| Start + Select | Exit |

## What runs it

Display, sound and pads go through the CFW's own SDL 2 (the bundled `libSDL3.so.0` is the
[SDL3-over-SDL2 shim](https://github.com/bmdhacks/SDL/tree/sdl2-backend) Dusklight also ships), so
KMSDRM, fbdev and Wayland CFWs are all fine and nothing on the device is replaced. Rendering needs the
CFW's OpenGL ES 3.1 driver (Mali-G31/G52 and newer, or Panfrost). On a Mali driver that drops draws
(libmali g13p0 on Knulli, g24p0 on ROCKNIX nightlies before about August 2026) the game shows a
"GPU driver update needed" notice and a banner along the bottom edge and runs correctly but slowly.
On ROCKNIX, update to 20260901 or newer (libmali g29p1) or switch the GPU driver setting to Panfrost;
please include the driver line from that notice in a report.

A device that cannot draw 60 frames a second still plays single-player at full speed: the game
simulates every frame and draws fewer of them, as the console does under load. `log.txt`'s `[perf]`
lines show the picture rate (`presented_fps`) and the game rate (`logic_fps`) separately;
`dropped_periods` above zero means the device could not keep up even so. Online matches keep one
drawn frame per game frame on both consoles, so they run at the slower device's frame rate.

## Reporting problems

Attach `ports/melee/log.txt` (written on every launch) and say which device, CFW and GPU driver you
used. If the display cannot be opened, that log lists the video drivers, the `/dev/dri` nodes and
SDL's reason for rejecting each driver. If the game crashes, the log ends with `[crash]` lines that
locate it, and the next launch starts with a fresh shader cache by itself when the crash happened
during start-up.

## Files

| File | Purpose |
| --- | --- |
| `melee.zip` | The PortMaster port ({{SIZE}} bytes, SHA-256 `{{SHA256}}`) |
| `melee-portmaster-symbols.tar.gz` | Unstripped `melee.aarch64` matching this zip, for crash backtraces |
