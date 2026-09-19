## Notes

Thanks to the [doldecomp/melee](https://github.com/doldecomp/melee) contributors, whose
decompilation turned Melee's game code back into C that compiles for any CPU, and to
[encounter/aurora](https://github.com/encounter/aurora) and
[encounter/dawn](https://github.com/encounter/dawn), which render the GameCube graphics calls on
OpenGL ES. Thanks also to the PortMaster team and the Dusklight porters whose launcher this one
follows.

This is a native AArch64 build of *Super Smash Bros. Melee* (US 1.02) that runs on the device's
own OpenGL ES driver. It includes no game data. You need your own dump of a Super Smash Bros.
Melee disc (US, version 1.02, `GALE01`). Port source:
[zalo/melee, branch `portmaster`](https://github.com/zalo/melee/tree/portmaster).

Mali-G52 devices (RK3566: Miyoo Flip, RGB30, RG353) need libmali g2p0 (stock Miyoo Flip firmware,
spruceOS) or g29p1 (ROCKNIX 20260901 and newer, dArkOS). g13p0 (Knulli) and g24p0 (ROCKNIX
nightlies before about August 2026) draw stale tiles; on ROCKNIX, update, or switch the GPU driver
setting to Panfrost, which renders correctly at 40-50 FPS. During the first menus the port
renders a few frames offscreen, with and without a per-draw texture-fetch barrier, and compares
them. If they differ, the barrier stays on, and a "GPU driver update needed" notice shows the
driver version for 20 seconds. Rendering is then correct but runs at about 9-12 FPS.
`AURORA_GLES_DRIVER_PROBE=0` disables the check.

Display, sound and pads go through the CFW's own SDL 2. The game links SDL 3 (Aurora renders
through it) as a shared library, and `melee/libs.aarch64/libSDL3.so.0` is the
[SDL3-over-SDL2 shim](https://github.com/bmdhacks/SDL/tree/sdl2-backend) that Dusklight also ships:
an SDL 3 whose video, audio and joystick drivers hand everything to the `libSDL2-2.0.so.0` already
on the device. So whatever the CFW's SDL 2 was patched for (KMSDRM with the RG351P/RG552 rotation,
fbdev on muOS and H700 Knulli, Wayland on ROCKNIX, the CFW's audio server) applies to Melee as well,
and nothing on the device is replaced. The launcher hands the CFW's `SDL_VIDEODRIVER` and
`SDL_AUDIODRIVER` to that inner SDL 2 unchanged. Dawn renders into a pbuffer-backed swapchain and
the port's present thread copies each frame into SDL's window surface. When the display cannot be
opened, `melee/log.txt` lists the video drivers in the build, the `/dev/dri` nodes and SDL's reason
for rejecting each driver.

## Installation

1. Dump your disc following the
   [Dolphin ripping guide](https://wiki.dolphin-emu.org/index.php?title=Ripping_Games).
   `.iso`, `.gcm`, `.ciso` and `.rvz` all work.
2. Copy the image into `melee/assets/`.
3. Answer **Yes** at the memory-card prompt on first boot. Saves live in `melee/runtime/config`.

The first matches after installing stutter while shaders compile; later runs use the
pipeline cache in `melee/runtime/cache`.

## Controls

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

## Online play (two devices on the same Wi-Fi)

Both devices must run the same release of this port. In VS Mode > Melee, on the character select
screen, press **Z** (R1): an overlay offers **Host a match**, a **Join** row for every other device
hosting on the network, and the input delay (**Auto** measures the connection). One player hosts,
the other joins; both games restart together (about ten seconds, with the host's save so unlocks
agree) and return to the character select screen, where the host is player 1 and the guest player 2.
Pick fighters and a stage as usual. A status line along the top shows the connection; a desync or a
lost connection turns it red, and the game continues offline from there. The match runs at the pace
of the slower device. The same options are in Options > Port Settings > Online Play.

## Port Settings

Main menu > **Options** > the fourth, unlabelled-on-disc row (it reads **Port Settings** here)
opens this port's own options screen. Left and right change a value, A activates a row, B saves
and returns. Values live in `melee/runtime/config/melee-native/settings.cfg`.

| Row | Effect |
| --- | --- |
| Debug Menu, Y on title | On: Y on the title screen opens the game's developer menu (below). Off by default. |
| Debug Overlays | On: matches gain the development tools below while gameplay stays retail. |
| Unlock All Characters and Stages | Sets every character and stage unlock (and the game's own "special message" bonuses) in the current save and writes the memory card. On the next visit to the main menu the game hands out the trophies it awards for those unlocks, one pop-up each; press A through them once. |
| Online Play | Reserved for the online-play configuration screen; shows "Coming soon". |

## Developer menu

With Debug Menu on, press **Y on the title screen** to open the game's own developer menu (the
retail game ignores Y there). Its first row, **Port Settings**, mirrors the screen above and adds:

| Row | Effect |
| --- | --- |
| DbLevel (next launch) | Forces a development debug level at the next launch (Debug-Rom is the full development build: stale-move decay off, self-destruct chords, live asserts). Leave on Retail unless you know the debug build. |

With Debug Overlays on, in any VS-style match (handheld buttons; the left stick is the GameCube
D-pad):

| Chord | Tool |
| --- | --- |
| R2 + left stick up | Cycle the collision-bubble view for every fighter (three steps; the second draws hurtboxes and hitboxes in place of the models) |
| R2 + left stick left | Cycle extra ranges (ledge grab, throws, item pickup, coin pickup) |
| Y (hold) + left stick down | Toggle the action-state / animation info panel |
| X (hold) + left stick down | Cycle the HUD off and on |
| X (hold) + left stick up | Debug pause (the retail Start pause keeps working) |
| R1 (Z) while debug-paused | Advance one frame |
| Y (hold) + left stick left/right | Camera info and free camera (C-stick moves it) |

The launcher environment can force any of these for one run: `MELEE_DEBUG_MENU=1`,
`MELEE_DEBUG_OVERLAYS=1`, `MELEE_DEBUG_LEVEL=3`.

## Building

The port is cross-compiled on an x86_64 Linux host with the Bootlin
`aarch64--glibc--stable-2023.08-1` toolchain for `-mcpu=cortex-a35`, so one binary runs on every
supported SoC. The C++ runtime is linked statically. SDL 3 is linked as a shared library and the
only bundled library is the SDL3-over-SDL2 shim (`native/tools/build_sdl3_shim.sh` builds it from
bmdhacks' SDL fork with the same toolchain; `MELEE_SDL=shim`, the default of
`build_portmaster.sh`). `MELEE_SDL=static` instead builds SDL 3 in with its own KMSDRM and Wayland
drivers and ALSA/PulseAudio/PipeWire backends (the Flip development flow; it cannot run on fbdev
CFWs). Either way `native/platform/flip/check_sdl_backends.sh` verifies the result: the shim mode
needs a dynamically linked `libSDL3.so.0` and a shim that carries the `sdl2` driver, the static mode
every backend in SDL's generated configuration and in the binary (SDL drops a backend silently when
its pkg-config module is missing at configure time, which once shipped a build without KMSDRM).

The release binary needs at most `GLIBC_2.30` (`min_glibc` in `port.json`), so that it also starts on
ArkOS (glibc 2.30) and CrossMix (2.33). The SDK's own glibc is 2.37, so `build_portmaster.sh` links
against a copy of the SDK whose sysroot is swapped for Bootlin `stable-2020.02-2`'s glibc 2.30
(`native/tools/glibc230_toolchain.sh build`; it downloads that toolchain once, keeps the SDK's
compiler, gcc 12 runtime and device libraries, and adds `native/tools/glibc_compat.c` to
`libstdc++.a` for the two symbols glibc 2.30 lacks). The build ends with a check that the binary
imports nothing newer than `GLIBC_2.30` and no shared `libstdc++`. Set `MELEE_MIN_GLIBC=sdk` to skip
the swap and link against the plain SDK (the result then needs glibc 2.34 or newer on the device).

1. Get the source:

   ```sh
   git clone -b portmaster https://github.com/sh1ftmaker/melee-native-miyoo-flip.git melee
   cd melee
   ```

2. Install host Rust under the build tools directory:

   ```sh
   export RUSTUP_HOME=$PWD/build/flip-tools/rustup CARGO_HOME=$PWD/build/flip-tools/cargo
   curl -sSf https://sh.rustup.rs | sh -s -- -y --no-modify-path --default-toolchain stable
   ```

3. Connect a supported device over ADB once. `prepare_flip.py` downloads the toolchain, pulls
   the device's GLES/GBM/DRM/ALSA libraries into it (only to generate link stubs; none of them
   ship in the port) and builds Dawn.

4. Build and package:

   ```sh
   sh native/platform/portmaster/build_portmaster.sh
   ```

   The zip and the unpacked port are written to `dist/portmaster/`.
