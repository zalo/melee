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

Mali-G52 devices (RK3566) need libmali g2p0 (stock Miyoo Flip firmware, spruceOS) or g29p1
(ROCKNIX, dArkOS). g13p0 (Knulli) and g24p0 draw stale tiles. During the first menus the port
renders a few frames offscreen, with and without a per-draw texture-fetch barrier, and compares
them. If they differ, the barrier stays on, and a "GPU driver update needed" notice shows the
driver version for 20 seconds. Rendering is then correct but runs at about 9-12 FPS.
`AURORA_GLES_DRIVER_PROBE=0` disables the check.

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
supported SoC. The C++ runtime is linked statically, so the port bundles no libraries. SDL is built
in with its KMSDRM and Wayland video drivers and loads libdrm/libgbm or libwayland from the CFW at
run time.

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
