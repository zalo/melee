# Super Smash Bros. Melee (native) for PortMaster, {{TAG}}

Native AArch64 build of *Super Smash Bros. Melee* (US 1.02) for PortMaster CFWs, built from
[zalo/melee `{{SHORT}}`](https://github.com/zalo/melee/commit/{{COMMIT}}) on {{DATE}}. It contains
no game data: you need your own dump of the disc (US, version 1.02, `GALE01`).

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
| D-pad | Control stick |
| Left stick | D-pad (taunt) |
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
(Knulli's g13p0 on the Miyoo Flip, for one) the game shows a "GPU driver update needed" notice and a
banner along the bottom edge and runs correctly but slowly; please include the driver line from that
notice in a report.

## Reporting problems

Attach `ports/melee/log.txt` (written on every launch) and say which device, CFW and GPU driver you
used. If the display cannot be opened, that log lists the video drivers, the `/dev/dri` nodes and
SDL's reason for rejecting each driver.

## Files

| File | Purpose |
| --- | --- |
| `melee.zip` | The PortMaster port ({{SIZE}} bytes, SHA-256 `{{SHA256}}`) |
| `melee-portmaster-symbols.tar.gz` | Unstripped `melee.aarch64` matching this zip, for crash backtraces |
