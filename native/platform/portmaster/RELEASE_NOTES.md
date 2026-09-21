# Super Smash Bros. Melee (native) for PortMaster, {{TAG}}

Native AArch64 build of *Super Smash Bros. Melee* (US 1.02) for PortMaster CFWs, built from
[zalo/melee `{{SHORT}}`](https://github.com/zalo/melee/commit/{{COMMIT}}) on {{DATE}}. It contains
no game data: you need your own dump of the disc (US, version 1.02, `GALE01`).

## Changes in this build

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
