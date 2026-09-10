# Miyoo Flip V2 (experimental)

Start with the [consolidated handoff](FLIP_HANDOFF.md) for the current source and
device state, measured performance, validated defaults, and remaining work.

This port targets the Flip's Linux AArch64 userspace and 640×480 panel. The
tested device uses Surwish OS with a Buildroot userspace and kernel 5.10.160.
The optional app-local g29p1 GLES driver improves compatibility without changing
firmware, the kernel, or MainUI's graphics libraries.

The current renderer decodes GX attributes on the FIFO worker into compact
conventional vertex buffers. Stable 64 KiB parameter tables and per-primitive
record indices allow adjacent compatible draws to batch while preserving TEV,
texture, blend/depth and EFB-copy ordering. g29 supports this path without the
old per-draw texture-fetch barrier. The installed g13 driver retains barriers
as a fallback. No vertex-stage SSBOs or multi-draw extensions are required.
The original integer-texture vertex path remains available for diagnostics.

The g29 launcher now uses direct GLES submission for supported GX passes,
cached resource bindings, NEON vertex decoding, changed-range uploads, and a
separate GLES presentation worker. Supported GX passes encode resource references
without redundant Dawn draws. Dawn still owns resources and shader compilation;
this is not yet a standalone GLES backend. Unsupported passes retain the
reference renderer. The independent native GLSL generator is experimental.

The [latest optimization report](validation/2026-09-09-flip/PIPELINED_PRESENT_ITERATION.md)
records exact capture comparisons and controlled timing windows: the combined
changes average 30.1 ms on frozen Onett and 19.7 ms on frozen Battlefield.
Readback counters confirm only the initial diagnostic screenshot, with no
recurring readback in either measured window. **60 FPS has not been achieved.** The earlier
[backend rewrite report](validation/2026-09-09-flip/BACKEND_REWRITE.md) records
the initial driver and vertex-input investigation. Desktop Linux and macOS
rendering paths retain their existing behavior.

The launcher temporarily selects the kernel's CPU/GPU/memory performance governors,
enables available CPU cores, and restores their original state on exit.
`MELEE_FLIP_PERFORMANCE=0` retains the firmware's governors;
`MELEE_FLIP_DIRECT_GLES=0` selects reference submission. g13 defaults to the
barrier-protected reference path. `MELEE_FLIP_NATIVE_SPECIALIZED=1` enables the
experimental native GLSL path; it remains slower and has small image differences.
The [draw-pattern profiling report](validation/2026-09-09-flip/DEEP_PROFILE_AND_STREAMING.md)
records native streaming experiments and nested CPU measurements. Configure
with `-DMELEE_FLIP_DEEP_TIMERS=ON`, then set `MELEE_FLIP_DEEP_PROFILE=1` to
trace selected frames. Normal builds compile those scopes out. Set
`MELEE_FLIP_STREAM_UPLOAD=1` to test fenced persistent native uploads. Both
remain opt-in; profiling timings include observer cost.
`MELEE_FLIP_PRESENT_THREAD=0` restores synchronous EGL presentation;
`MELEE_FLIP_DIRTY_UPLOAD=0` disables changed-range upload caching. Explicit
settings are preserved. The decoded-vertex content cache remains opt-in because
its measured frame-time improvement is negligible.

## Build

Host requirements: Linux, Clang (tested with 22.1.8), CMake 3.25+, Git,
Python 3.11+, Rust/Cargo (tested with 1.98.1), and ADB. Install EGL/GLES, GBM,
DRM, ALSA and udev development headers on the host, or set FLIP_HOST_HEADERS
to a directory containing them. These portable headers are copied into an
isolated cross SDK. The device provides its own matching link libraries.

```sh
rustup target add aarch64-unknown-linux-gnu
python3 native/tools/prepare_flip.py --adb adb --with-g29
export FLIP_TOOLCHAIN="$PWD/build/flip-tools/aarch64--glibc--stable-2023.08-1"
export FLIP_DAWN_PREFIX="$PWD/build/flip-tools/dawn-install"
native/platform/flip/build.sh
cmake --build build/native-flip --target melee_flip_gpu_probe --parallel 6
python3 native/tools/package_flip.py --mali-g29 build/flip-tools/mali-g29p1-candidate/libmali.so.1
```

`prepare_flip.py` downloads checksum-pinned Bootlin GCC 12.3 SDK and Dawn
sources, checks/applies the tracked patches, reads graphics/audio libraries
from the Flip over ADB, and builds Dawn. It preserves already prepared sources
and rejects conflicting patch state. `--prepare-only` skips compiling Dawn.
The bootstrapped Aurora revision remains
749d6ee7a22bdfab78c8ece9047bca5d79aa72ca; Dawn remains
1155e0ed531126f33a1279afa029349651ca1c93, with the explicit local patches.

The C/C++ wrappers pass the target and sysroot even when dependency generators
invoke the compiler outside CMake's normal compile rule. nod is built from
source because its release has no Linux ARM64 library package. Dawn and SDL
are built from source rather than using desktop binaries. Build directories
and downloaded SDKs are ignored by Git.

The SDK's C++ runtime is bundled privately because the installed system's
libstdc++ lacks GLIBCXX_3.4.30. Packaging checks AArch64 ELF headers and rejects
requirements newer than the tested glibc 2.36. The optional `--with-g29` preparation step fetches a checksum-pinned driver;
`package_flip.py --mali-g29 PATH` verifies it again and includes its licence.
No libc, ROM, extracted assets, fonts, or firmware are packaged.

## Install and update

```sh
# Exactly once: nodtool is from encounter/nod v2.0.0-alpha.10.
python3 native/tools/flip_deploy.py disc /path/to/melee-us-1.02.rvz --nodtool /path/to/nodtool
# For each build: only allowlisted runtime files are uploaded.
python3 native/tools/flip_deploy.py build dist/flip/Melee-Native-Flip
adb -s 10.0.0.178:5555 push 'native/platform/flip/Melee Native.sh' /mnt/SDCARD/Roms/PORTS/
```

The persistent image is `/mnt/SDCARD/Ports/melee-native/data/disc.img`. Format
is detected from its contents; RVZ works without conversion. Disc installation
verifies the image using nod, verifies SHA-256 after transfer, and renames the
partial file only after success. Repeating the disc command skips an identical
image and refuses to overwrite a different existing image. Build deployment
cannot include a ROM and compares hashes before transferring runtime files.

Open **PORTS → Melee Native**, or run
`/mnt/SDCARD/Ports/melee-native/launch.sh`. Refresh the Ports list if necessary.
The launcher writes logs to `data/state/game.log` and keeps settings and shader
cache under `data/`. Choose **No** at the memory-card creation prompt; saving
is an inherited incomplete feature. Select+Start exits through the normal
shutdown path. The default SDL mapping uses the bottom face button for A and
the right face button for B; consult the bundle README for controls.

## Validation

The ROM-free GPU probe initializes the real EGL/GLES driver. With `--present`
it renders seven phases of 60 frames: direct vertices, indexed arrays across
a texture-row boundary, textured/depth-tested vertices, big-endian positions,
a transformed draw, two transformed direct draws, and two transformed indexed
draws. Readback checks colors, image changes, and expected triangle positions;
a solid or frozen framebuffer fails.
Readback is saved to `/tmp/melee-flip-probe/frame.ppm` on the device.

```sh
adb -s 10.0.0.178:5555 shell 'cd /mnt/SDCARD/Ports/melee-native && LD_LIBRARY_PATH="$PWD/lib" ./melee_flip_gpu_probe --present'
python3 native/tests/test_flip_deploy.py
python3 native/tests/test_flip_prepare.py
```

The probe changes the active display temporarily and restores the previous
CRTC when it exits normally. Game integration tests use the existing
`native/tests/match-controls.input`, `MELEE_INPUT_SCRIPT`, `MELEE_TEST_SEED=1`,
`MELEE_RENDER_CHECK=1` and `MELEE_AUDIO_CHECK=1`, with the installed image.
See the [device report](validation/2026-09-09-flip/REPORT.md) for actual results.
Do not interpret a cross-build or the triangle probe as gameplay acceptance.

## Frame timing profiler

Set `MELEE_FLIP_PROFILE=1` when launching to emit one `[flip-profile]` record
per presented frame. The default launcher leaves profiling off. Records report
CPU wall time in milliseconds for graphics submission, draw calls, barriers,
EGL presentation, GBM buffer acquisition, and DRM page flipping. Draw/barrier
times are **included in** submission time; do not add them again. These are not
GPU timestamp queries or exclusive game-logic measurements. The existing
`[perf]` and `[pacing]` records provide five-second FPS and frame-time windows.

```sh
python3 native/tools/analyze_flip_profile.py game.log --csv frames.csv
# Scripted tests log scene transitions; scene 2 is gameplay.
python3 native/tools/analyze_flip_profile.py game.log --scene 2 --csv match.csv
```

The first 70-frame Onett gameplay profile averaged 309.6 ms/frame, with
279.3 ms in driver submission (including 108.8 ms drawing and 124.3 ms barriers).
EGL swap averaged 3.1 ms and DRM presentation 9.2 ms. This identifies graphics
submission/synchronization as the primary bottleneck. See the validation report
for evidence and rejected workaround experiments.

## Threading experiments

The renderer already uses separate FIFO and render workers. On the tested
firmware, CPUs 2 and 3 were offline; allowing all four cores improved short
match samples from roughly 3.3–3.5 FPS to 4.3–4.4 FPS. Fixed thread pinning was
slower, and FIFO batching showed no clear benefit. Longer tests revealed a
separate memory-pressure failure. See the [threading report](validation/2026-09-09-flip/THREADING.md)
for measurements, research sources and candidate validation status.

The launcher temporarily enables offline CPU cores and selects supported
performance governors, restoring both on exit. Set `MELEE_FLIP_ALL_CORES=0`
to retain the firmware's core selection and `MELEE_FLIP_PERFORMANCE=0` to
retain its clock governors.

### Barrier diagnostics

The g13 fallback retains every per-draw texture-fetch barrier; g29 uses the
qualified conventional-input path without them. For reference-path
experiments, `MELEE_FLIP_BARRIER_MASK` is a 64-bit keep mask (base-0 integer,
including hex). Bit 0 controls the barrier after draw 1; bit 63 controls draw
64. `MELEE_FLIP_BARRIER_BASE=64` moves that window to draws 65–128. Draw
numbers reset at each actual frame submission; auxiliary submissions and
calls outside the window retain barriers. `MELEE_FLIP_BARRIER_TRACE=1` logs
draw ordinals, type/count, and whether each barrier was applied. Ordinals can
refer to different content in different frames and are diagnostic, not stable
material identifiers.

`MELEE_FLIP_BARRIER_EVERY=N` additionally keeps only every Nth frame-draw
barrier (g13 default 1, g29 default 0; 0 removes all). Removing barriers on
g13 can cause missing geometry and GPU faults. The direct diagnostic mode
bypasses the reference interposers; select `MELEE_FLIP_DIRECT_GLES=0` for
barrier-mask trials.

The probe accepts `MELEE_PROBE_DRAWS=2..16` and
`MELEE_PROBE_FRAMES=1..60`. Compare isolated all-barrier controls with candidate
masks using `native/tools/analyze_flip_barriers.py DIRECTORY --reference control`.
A short cold-cache failure is not adequate evidence: repeat full-length runs.

Real-game trials use `native/tools/flip_game_barrier_trial.sh` through the
stock MainUI handoff. Supply `/tmp/game-barrier-name`, `/tmp/game-barrier-mask`,
`/tmp/game-barrier-base`, the scripted `/tmp/melee-match-controls.input`, and
`/tmp/melee-scanout` diagnostic executable. The script stops after a match
capture, 180 seconds, or less than 180000 kB MemAvailable; it preserves logs,
kernel messages, EFB/panel captures, and core-restoration evidence under
`/tmp/game-barrier-bisect`. It uses the already-installed disc image.
`native/tools/analyze_flip_game_barriers.py DIRECTORY` rejects incomplete
runs, differing EFB captures, or new GPU faults. Panel captures also require
inspection; a passing short trial does not establish correctness across
scenes or long play.

Live changes require no restart: the render worker polls
`/tmp/melee-flip-barriers.conf` every 250 ms at frame boundaries. The file is
three whitespace-separated integers: `MASK BASE EVERY`. Replace it atomically;
malformed input leaves the current settings intact. Removing it restores the
startup environment/defaults. `MELEE_FLIP_BARRIER_CONFIG` overrides its path.
Each accepted change logs `[flip-barrier-config]` with its first frame number.
A one-time GPU drain and transition barrier order preceding work; they cannot repair already-corrupted
resources or recover a hung GPU context.

```sh
# Restore every barrier immediately in the running game.
python3 native/tools/set_flip_barriers.py --adb build/flip-tools/platform-tools/adb
# Example: remove only the barrier after draw 13, keeping all other barriers.
python3 native/tools/set_flip_barriers.py --adb build/flip-tools/platform-tools/adb --mask 0xffffffffffffefff
# Remove the override and return to startup settings.
python3 native/tools/set_flip_barriers.py --adb build/flip-tools/platform-tools/adb --reset
```

A live setting lasts across game launches until reset or the device reboots.
Always clear experimental settings after a session. Compare frame-time windows
only after observing acknowledgement, and repeat all-barrier controls between
suspect masks. Changing settings in a paused scene can hold gameplay state
steady; scene/draw identities still need confirmation from traces.


Systematic fixed-scene screening is available through
`native/tools/sweep_flip_barriers.py --adb build/flip-tools/platform-tools/adb --output NEW_DIRECTORY`.
It uses explicit `/dev/kmsg` markers instead of comparing kernel timestamps
with `/proc/uptime` (these clocks are offset on the tested firmware). It keeps
passing removals cumulatively, revokes recent removals when combined recovery
fails, and performs a longer final check. Results apply to the tested scene;
normal startup settings are restored when the tool exits.

For a stable real-game image, an authorized test launcher can set
`MELEE_MATRIX_TEST=1 MELEE_TEST_FREEZE_AFTER=45`. Simulation freezes after that
many match updates, while actual GX drawing and audio continue. This is a
diagnostic freeze, not a display failure; restart without the variable for
normal gameplay. `touch /tmp/melee-flip-trace.request` requests one frame of
shader-program, framebuffer, index-buffer and UBO-range traces in `game.log`.

The standalone `native/tools/flip_gl_hazards.c` compares vertex attributes,
integer-texture vertex fetching, changing UBO ranges, separate UBOs, and fixed
UBO arrays. It intentionally reports failures on the stock driver. See
[the root-cause investigation](validation/2026-09-09-flip/ROOT_CAUSE.md).
The interposers under `native/tools/flip_plain_uniforms.cpp` and
`native/tools/flip_uniform_windows.cpp` are experimental and are not included
in the normal launcher or runtime bundle.

## Backend controls and reproducible trials

Normal launch selects the bundled g29 driver when present, with vertex inputs,
uniform tables, adjacent batching, and no per-draw barriers. Select
`MELEE_FLIP_DRIVER=g13` for the installed-driver fallback; it retains barriers
and disables the new batching by default. `MELEE_FLIP_VERTEX_INPUT`,
`MELEE_FLIP_UNIFORM_TABLE`, and `MELEE_FLIP_BATCH_DRAWS` accept explicit `0`/`1`
diagnostic overrides. A shader-cache directory can be selected with
`MELEE_FLIP_CACHE_HOME`. On-demand compilation avoids prewarming unrelated
stages and historical test configurations; `MELEE_FLIP_PREWARM_PIPELINES=1`
restores the original prewarming for comparison.

Build `melee_flip_vertex_test` for the CPU decoder fixture and
`melee_flip_scanout` for DRM readback. The latter supports imported dma-bufs
and fails without leaving stale output. The trial tool uploads these small
diagnostics and its input script, never a ROM:

```sh
cmake --build build/native-flip --target melee_flip_vertex_test melee_flip_scanout
python3 native/tools/flip_backend_trial.py onett-check --batch 1
python3 native/tools/flip_backend_trial.py battle-check --batch 1 --stage 31 \
  --freeze-after 300 --capture-frame 360
python3 native/tools/flip_backend_trial.py moving-check --batch 1 \
  --freeze-after 0 --soak 90
```

Each unique trial name preserves the script, kernel marker, memory samples,
log, EFB capture and physical scanout under the validation directory. Fixed
simulation states are diagnostic only; normal launch remains interactive.
