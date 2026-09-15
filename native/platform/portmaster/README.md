# Super Smash Bros. Melee (native) for PortMaster

A native AArch64 build of *Super Smash Bros. Melee* (US 1.02), compiled from the
[doldecomp/melee](https://github.com/doldecomp/melee) decompilation and rendered
through [Aurora](https://github.com/encounter/aurora) (GameCube GX on OpenGL ES via
[Dawn](https://github.com/encounter/dawn)). This port carries the Aurora
performance PR set (CPU vertex decode, resident display lists, stable texture
identities, pipeline-state memo, async frames, texture arrays and atlas, pass
fusion, instanced point sprites) and the direct GLES submission fast path that
took the game from 3 FPS to the 60 Hz boundary on a Miyoo Flip.

Huge thanks to the [doldecomp/melee](https://github.com/doldecomp/melee)
contributors, to [encounter/aurora](https://github.com/encounter/aurora) and
[encounter/dawn](https://github.com/encounter/dawn) for the renderer this port
builds on, and to the [PortMaster](https://portmaster.games) team and the
Multiverse Dusklight porters whose launcher this one follows.

> [!IMPORTANT]
> This port includes no game data and never will. You need your own dump of a
> Super Smash Bros. Melee disc you own (US, version 1.02, `GALE01`).

## Install

1. Install `melee.zip` through PortMaster (or unzip `Melee.sh` and `melee/` into
   your `ports/` directory).
2. Dump your GameCube disc following the
   [Dolphin ripping guide](https://wiki.dolphin-emu.org/index.php?title=Ripping_Games).
   `.iso`/`.gcm` are used as-is; `.ciso` and `.rvz` (Dolphin or
   [nodtool](https://github.com/encounter/nod/releases)) are read directly and
   save space.
3. Copy the image into `melee/assets/`. The launcher uses the first image it finds
   there (`MELEE_PM_DISC=/path/to/image` overrides).
4. Launch *Super Smash Bros. Melee (native)*. Answer **Yes** at the memory-card
   prompt on first boot; the save is a Dolphin GCI folder under
   `melee/runtime/config/melee-native/USA/Card A/` (copy that folder to back it up).

Only the US 1.02 release is supported. Logs are in `melee/log.txt` (previous run:
`log.prev.txt`) and `melee/runtime/state/melee-native/`.

## Controls

The game reads the gamepad itself through SDL (the CFW's controller mapping from
`get_controls` is passed in). `gptokeyb` is only used for the exit hotkey.

| Handheld | Game |
| --- | --- |
| D-pad | Control stick (movement) |
| Left stick | GameCube D-pad (taunt) |
| Right stick | C-stick |
| A (right face button) | A: attack / confirm |
| B (bottom face button) | B: special / cancel |
| X, Y | Jump |
| R1 | Grab (Z) |
| L2 / R2 | Shield (L / R) |
| Start | Pause |
| Start + Select | Exit the port |

This is the Miyoo Flip mapping: the D-pad drives the control stick and the face
buttons follow Nintendo labelling (B below A), which is the reverse of the Xbox
layout the gamepad driver reports. The swap is a runtime switch, not hard-coded:
export `MELEE_PM_SWAP_CONTROLS=0` before launching (for example in a
`mod_<CFW>.txt`-style wrapper or by editing `Melee.sh`) to get Aurora's stock
mapping instead (left stick moves, bottom face button is A, right face button is
B). The same variable is also honoured under its Flip name
`MELEE_FLIP_SWAP_CONTROLS`.

## Devices and display

The binary opens the display itself over DRM/GBM/EGL (no SDL video, no X11 or
Wayland) and uses the panel's preferred mode: on a 640x480 panel it renders at
the game's native 640x480; on other panels it renders at the panel's mode size.
Panel rotation is not attempted. `MELEE_DRM_DEVICE=/dev/dri/cardN` and
`MELEE_DRM_CONNECTOR=<index>` pick the DRM node and connector when the first
connected one is wrong.

- **RK3566 with a 640x480 panel (Miyoo Flip V2)**: the proven target. Everything
  in the performance section was measured there.
- **Other RK3566 devices (RG353 family, 640x480 and 1280x720 panels)**: same SoC
  and GPU driver class; the >480p path is untested.
- **RK3326, H700, RK3588, Snapdragon**: untested. RK3326 has half the CPU and a
  Mali-G31; the fast path matters even more there and 60 FPS is unlikely.
  Devices whose CFW GLES driver lacks `GL_EXT_buffer_storage` or GLES 3.1 fall
  back inside Aurora but have not been run.

GPU driver: by default the CFW's own GLES driver is used. If the zip was built with
the optional `lib/mali-g29p1/libmali.so.1` (`--mali-g29`) and the device tree says
`rk3566`, that driver is put first on `LD_LIBRARY_PATH` instead (it is the blob the
Flip numbers were measured with; the stock g13 driver needs a per-draw barrier the
fast path removed). `MELEE_PM_DRIVER=system` or `=bundled` overrides.

## Performance

Measured on a Miyoo Flip V2 (RK3566, 4x Cortex-A55 1.99 GHz, Mali-G52 MP2,
640x480) with the launcher's governors and all four cores online; details and the
per-optimization table are in `native/FLIP_PERFORMANCE_WINS.md` of the source tree.

| Stage | Frozen Onett frame time |
| --- | --- |
| Stock driver, first build | 309.6 ms (3-4 FPS) |
| CPU vertex decode, batching, g29 driver | 106.5 ms (9.4 FPS) |
| Direct GLES submission | 38.6 ms |
| Presentation worker, dirty uploads | 29.9 ms |
| Aurora PR set + GLES fast path (this port) | 16.7 ms median, 52-59 FPS in moving Onett, Battlefield, Pokemon Stadium, Hyrule |
| Fountain of Dreams (25k point sprites) | 23.9 ms, ~37-40 FPS |

The launcher sets these fast-path defaults and passes through anything you set:

| Variable | Default | Meaning |
| --- | --- | --- |
| `MELEE_FLIP_PRESENT_THREAD` | 1 | presentation on its own thread |
| `MELEE_FLIP_ASYNC_PRESENT` | 1 | asynchronous DRM page flips |
| `MELEE_FLIP_DIRECT_GLES` | 1 | direct GLES submission of GX passes |
| `MELEE_FLIP_CPU_VERTEX_DECODE`, `MELEE_FLIP_RESIDENT_DL`, `MELEE_FLIP_ASYNC_FIFO`, `MELEE_FLIP_TEXTURE_ATLAS`, `MELEE_FLIP_FUSE_PASSES`, `MELEE_FLIP_UNIFORM_TABLE`, `MELEE_FLIP_BATCH_DRAWS`, `MELEE_FLIP_MAPPED_STREAM`, `MELEE_FLIP_SCENE_ON_SURFACE` | 1 | Aurora renderer options (=0 disables one for an A/B test) |
| `MELEE_FLIP_TEXTURE_ATLAS=0` | | restores bit-exact output (the atlas moves ~100-170 pixels by one unit) |
| `MELEE_FLIP_PROFILE=1` | | per-frame timing lines in the log |
| `MELEE_PM_PERFORMANCE=0`, `MELEE_PM_ALL_CORES=0` | | leave governors / offline cores alone |

The pipeline cache lives in `melee/runtime/cache/pipeline` and is wiped
automatically when a newer `melee.aarch64` is installed; the first boot after
that compiles shaders on demand and stutters for a while.

## Building

Cross-compiled from Linux x86-64 with the Bootlin `aarch64--glibc--stable-2023.08-1`
toolchain (glibc 2.37 sysroot; the binary itself imports nothing newer than
GLIBC_2.36, which `package_flip.py` checks), a Dawn built with
`native/platform/flip/dawn-gl-interop.patch` (`zalo/dawn-aurora-arm`,
`gl-native-interop`), and Aurora `zalo/aurora-arm` branch `gles-direct-submission`
(pinned in `native/tools/bootstrap.py`). One command runs the whole chain:

```sh
# in the melee source tree (branch portmaster)
FLIP_TOOLCHAIN=build/flip-tools/aarch64--glibc--stable-2023.08-1 \
FLIP_DAWN_PREFIX=build/flip-tools/dawn-install \
MALI_G29=build/flip-tools/mali-g29p1-candidate/libmali.so.1 \
sh native/platform/portmaster/build_portmaster.sh
# -> dist/portmaster/melee.zip and dist/portmaster/melee/
```

`build_portmaster.sh` runs `bootstrap.py` (Aurora), `prepare_flip.py` when the SDK
or Dawn prefix is not given (it pulls the device's GLES/GBM/DRM libraries over ADB
once and cross-builds Dawn), `platform/flip/build.sh` (CMake cross build of
`melee_native`), and `tools/package_portmaster.py`, which reuses
`tools/package_flip.py` for the stripped binary, `libstdc++.so.6`, the optional
Mali driver and the license texts. `python3 native/tests/test_portmaster_package.py`
validates the port files and the zip.

## Licenses

The decompiled game code is from doldecomp/melee (no license claimed on the
recovered code; the game itself is Nintendo's property and is never
distributed). Bundled components keep their own licenses, all shipped in
`melee/licenses/`: Aurora (MIT), Dawn (BSD-3-Clause), nod (MIT), SDL (zlib),
Dear ImGui (MIT), Abseil (Apache-2.0), Tracy (BSD-3-Clause), xxHash (BSD-2-Clause),
FreeType (FTL), libpng (PNG Reference Library License), fmt (MIT), and the GCC
runtime library (`libstdc++.so.6`, GPL-3.0 with the GCC Runtime Library
Exception). When the optional Mali driver is bundled, its EULA and source notice
are included as `Mali.txt` and `Mali-source.txt`.
