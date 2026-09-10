# Miyoo Flip V2 port: consolidated handoff

Snapshot: 2026-09-10. This is the entry point for the ARM port, renderer work,
profiling results, and remaining optimization work. Build and installation
details are in [FLIP.md](FLIP.md).

**The game renders and plays with working audio and controls, but solid 60 FPS
has not been achieved.** The latest installed build averages about 30.1 ms on
the frozen Onett reference. Earlier moving gameplay samples average about
35.7 ms on Onett and 24.9 ms on Battlefield. The target is 16.67 ms per frame.

## Repository and device state

The port is published on the `miyoo-flip` branch of
[sh1ftmaker/melee-native-miyoo-flip](https://github.com/sh1ftmaker/melee-native-miyoo-flip),
based on `jonrosner/melee-native` commit `76e6bb95f`. Upstream history is retained.
The v71–v78 labels below identify development iterations, not release tags.

| Item | State at handoff |
| --- | --- |
| Hardware | Rockchip RK3566, Mali-G52, AArch64 Linux, 640×480, approximately 1 GiB RAM |
| Firmware | Surwish / Buildroot, kernel 5.10.160 |
| Device game executable | v77, gameplay-tested; detailed timers compiled out |
| Local executable and committed source | v78; cross-build and ARM decoder test pass; new opt-in paths not gameplay-tested |
| Device activity | Game stopped, MainUI running |
| Restored system state | CPUs 0–1; CPU `ondemand`, GPU `simple_ondemand`, DMC `dmc_ondemand` |
| ROM | Already installed at `/mnt/SDCARD/Ports/melee-native/data/disc.img`; RVZ content supported directly |
| Older package | Local `dist/flip/Melee-Native-Flip` retained as an older fallback; not the latest build |

The installed v77 executable SHA-256 is
`d582f44624c49caf925e2762b4958e0906bfccf2fe95e88e5ca7b6b77be0a999`.
The final CPU test upload changed only `/tmp/melee_flip_vertex_test`, not the
game executable. No ROM was transferred during optimization or handoff.

Source, dependency patches, tools, and written reports belong in the fork.
ROMs, extracted assets, SDKs, proprietary driver binaries, executable builds,
shader caches, and raw device captures remain local. Historical reports name
local evidence files; those references are not downloads from the fork.

## What changed and why

The native port uses a cross SDK, a private compatible C++ runtime, ARM fixes
in the recovered game/runtime code, RVZ reading through nod, and Flip controls,
audio, display ownership, packaging, and incremental deployment.

The GPU **does support vertex buffers**. The original integer-texture vertex
fetch path was a compatibility strategy, not a hardware limitation. The older
g13 driver reproduced missing geometry and GPU faults when its per-draw
texture-fetch barriers were removed. Conventional vertex inputs together with
the app-local g29p1 driver render correctly without those barriers. This
establishes a working combination; it does not identify a proprietary driver
bug at source level or prove Surwish itself caused it. Firmware, kernel, and
system graphics libraries were not replaced. The g13 fallback remains available.

The renderer now decodes GX attributes on the FIFO worker into compact vertex
buffers, including NEON fixed-point conversion. Stable 64 KiB uniform windows
hold sixteen 4 KiB draw records. Per-vertex record indices let adjacent compatible
draws batch while preserving ordering. Correcting the front-face convention
after clip-space Y inversion fixed missing/black surfaces.

Qualified GX passes use direct GLES draw submission with cached resource and
pipeline bindings; their Dawn encoding records resources without issuing the
same draws again. A shared-context presentation worker moves EGL/GBM/DRM
presentation off the render worker, using a bounded queue, GL fences, and
explicit texture ownership. Changed-range uploads avoid redundant data writes.

**Dawn is still present:** it owns resources, shader generation/compilation,
queue infrastructure, and reference passes. This is not a complete independent
GLES backend. The experimental native GLSL generator has small image differences
and was slower, so it is not the default.

The launcher enables available cores and supported performance governors, then
restores them on exit, failure, or handled termination. Supported maxima observed
were CPU 1992 MHz, GPU 900 MHz, and DMC 1056 MHz; firmware often selected 780 MHz
for DMC before the launcher changes. These are supported firmware settings.

## Current defaults and experiments

| Setting | g29 default / purpose |
| --- | --- |
| `MELEE_FLIP_VERTEX_INPUT=1`, `MELEE_FLIP_UNIFORM_TABLE=1` | Conventional vertices and stable draw parameter tables |
| `MELEE_FLIP_BARRIER_EVERY=0`, `MELEE_FLIP_BATCH_DRAWS=1` | No old per-draw barriers; adjacent compatible batching |
| `MELEE_FLIP_DIRECT_GLES=5`, `MELEE_FLIP_DIRECT_PACKET=1` | Direct submission for eligible GX passes |
| `MELEE_FLIP_DIRECT_CHECKS=0`, `MELEE_FLIP_FAST_VALIDATION=1` | Qualified fast submission settings |
| `MELEE_FLIP_ASYNC_PRESENT=1`, `MELEE_FLIP_PRESENT_THREAD=1` | Asynchronous presentation with a GLES worker |
| `MELEE_FLIP_DIRTY_UPLOAD=1` | Compare and upload changed ranges |
| `MELEE_FLIP_PERFORMANCE=0`, `MELEE_FLIP_ALL_CORES=0` | Optional opt-outs from launcher performance/core changes |

Explicit overrides are preserved. g13 defaults to barriers enabled, batching
disabled, and reference submission. Setting `MELEE_FLIP_DIRECT_GLES=0` selects
reference submission. The following experiments remain **off by default**:

- `MELEE_FLIP_STREAM_UPLOAD=1`: three persistently/coherently mapped native
  vertex/index/uniform buffer sets, with a fence per submission protecting reuse.
  Nonqualified consumers force reference uploads; transitions invalidate shadows.
  `MELEE_FLIP_STREAM_EVERY=2` exercises alternating fallback. Onett showed no
  convincing overall gain; frozen Battlefield improved by about 1 ms.
- `MELEE_FLIP_NATIVE_SPECIALIZED=1`: independent native GLSL generation; fidelity
  and performance still need work.
- `MELEE_FLIP_VERTEX_CACHE=1`: decoded-vertex content cache; many hits but negligible
  measured frame-time improvement.
- `MELEE_FLIP_MAP_UPLOAD=1`: map/invalidate upload experiment; negligible gain.
- **v78** `MELEE_FLIP_DECODE_DIRECT=1`: decode uncached, non-line primitives into
  final frame vertex storage, eliminating the temporary vector and staging copy.
  Cached and line paths keep their existing handling. Guarded differential ARM
  decoder tests pass, but gameplay and performance validation are pending.
- **v78** `MELEE_FLIP_RELEASE_TEXOBJ=1`: release temporary GX texture-object
  identities after loading them in `HSD_TObjSetup`. Intended to reduce object-cache
  churn while preserving content validation. Gameplay correctness and performance
  are unverified; do not assume it eliminates texture hashing.

## Performance evidence

These are last-120-sample means unless otherwise stated. Frozen workloads are
repeatable graphics tests, not proof of moving-game performance. Do not compare
different stages, capture policies, or diagnostic workloads as controlled A/Bs.

| Trial | Workload | Mean ms | Qualification |
| --- | --- | ---: | --- |
| v69-combined | Frozen Onett | 30.141 | Combined direct submission, upload and presentation changes |
| v70-readback-stage31 | Frozen Battlefield | 19.702 | Production-style path; exact reference captures |
| v70-launcher-moving-onett-clean | Moving Onett | 35.683 | Median 32.057; p95 39.931 |
| v70-launcher-moving-stage31 | Moving Battlefield | 24.882 | Median 24.526; p95 29.758 |
| v71-stream-control-onett | Frozen Onett | 30.190 | Reference for native upload experiment |
| v71-native-stream-onett | Frozen Onett | 30.022 | Native full-copy streaming, opt-in |
| v72-stream-stage31 | Frozen Battlefield | 18.761 | Native changed-range streaming, opt-in |
| v77-timers-compiled-out | Frozen Onett | 30.099 | Installed build; median 29.750; p95 33.387 |

The v77 EFB and scanout hashes exactly match their frozen Onett references, with
no new fault/error/hang/reset lines in the marked kernel interval. The moving
native/reference transition trial also completed without new marked faults, but
its capture is a different simulation tick, so byte equality was not asserted.

Earlier v61 direct/reference comparisons used repeated diagnostic capture:
Onett improved from 49.59 to 38.63 ms and Battlefield from 29.95 to 24.52 ms.
These establish improvement within that test, not a comparison with the newer
capture-once timings. Initial stale scanout evidence was rejected; reliable
fresh-scanout comparisons begin with the `batch-v7-*` trials.

## What remains slow

Recurring CPU GPU-readback is not the explanation in the measured scenes.
Counters show exactly one `glReadPixels` and one read-map for the requested
diagnostic screenshot, with no further increments through hundreds of frozen
and moving frames. Normal play does not request that screenshot. GPU EFB copies
remain GPU-local; write maps and fence waits are not CPU readback. Depth peek is
disabled in this compatibility configuration, and no Melee/HSD `GXPeekZ` callers
were found. Tracy timestamp readback is compiled out.

Deep v75 Onett traces contain 2,095 primitive preparations, 6,866 attribute
conversions, 350 actual GX GL draws, 393 pipeline builds, 471 uniform builds,
and 366 texture hashes covering 3,998,944 bytes per sampled frame.

| Instrumented host work | Inclusive wall ms/frame |
| --- | ---: |
| Vertex decoding | 10.837 |
| Attribute conversion, included in decoding | 7.976 |
| Pipeline configuration/build | 3.903 |
| Texture hashing | 2.558 |
| Vertex staging copy | 1.268 |
| GL draw calls | 5.880 |
| GL texture binding | 2.431 |
| GL uniform binding | 0.099 |

These scopes include instrumentation and scheduling costs, overlap when nested,
and run on different workers. **Do not sum this table into a frame budget.**
Common unchanged-program GL calls take roughly 12–14 microseconds; program-change
groups often take 18–23 microseconds. The repeated ordinary costs matter more
than one approximately 70-microsecond program used only twice per frame.

Common expensive format groups include INDEX16 S16 XY texture coordinates,
INDEX16 F32 XYZ positions, and INDEX16 S16 NBT normals. CPU return-address
sampling attributes substantial hashing to `hash_texture_source` and memory
comparison to `FlipStream::upload`. The persistent upload experiment reduced
bytes written but retained the cost of comparing the full active data.

Detailed profiling itself caused a regression: untraced v75 measured 32.099 ms,
v76 31.071 ms, and v77 restored 30.099 ms by compiling hooks out. Production
builds use `MELEE_FLIP_DEEP_TIMERS=OFF`. Detailed scopes require an ON build plus
`MELEE_FLIP_DEEP_PROFILE=1`; optional `MELEE_FLIP_DEEP_CPU=1` adds substantial
clock overhead. Use a separate timers-OFF build to qualify speedups. GPU timer
queries currently attribute passes, not individual draw groups.

## Reproduce and validate

For a fresh SDK and package, follow [FLIP.md](FLIP.md). For the prepared local
workspace, build without changing the installed executable:

```sh
export FLIP_TOOLCHAIN="$PWD/build/flip-tools/aarch64--glibc--stable-2023.08-1"
export FLIP_DAWN_PREFIX="$PWD/build/flip-tools/dawn-install"
export RUSTUP_HOME="$PWD/build/flip-tools/rustup"
export CARGO_HOME="$PWD/build/flip-tools/cargo"
export PATH="$PWD/build/flip-tools/cmake-3.31.6-linux-x86_64/bin:$CARGO_HOME/bin:$PATH"
cmake --build build/native-flip --target melee_native melee_flip_vertex_test --parallel 6
python3 native/tests/test_flip_launcher.py
python3 native/tests/test_flip_deploy.py
```

Aurora revision `749d6ee7a22bdfab78c8ece9047bca5d79aa72ca` and Dawn revision
`1155e0ed531126f33a1279afa029349651ca1c93` are modified through tracked patches:
[Aurora](platform/flip/aurora-flip.patch) and
[Dawn](platform/flip/dawn-egl-native-window.patch). Their working checkouts are
under ignored `build/` directories; the patches are essential for reproduction.
At handoff both patches pass reverse-application checks against those checkouts.
v78 cross-builds successfully, all 16 launcher/deployment tests pass, and the
v78 CPU vertex decoder test passes on the Flip, including poisoned output and
guard regions for the new direct-output decoder.

The working device connection is passwordless ADB at `10.0.0.178:5555`:

```sh
build/flip-tools/platform-tools/adb -s 10.0.0.178:5555 push build/native-flip/melee_flip_vertex_test /tmp/melee_flip_vertex_test
build/flip-tools/platform-tools/adb -s 10.0.0.178:5555 shell 'cd /mnt/SDCARD/Ports/melee-native && LD_LIBRARY_PATH="$PWD/lib/mali-g29p1:$PWD/lib:/usr/lib" /tmp/melee_flip_vertex_test'
```

For future deployment, package into a fresh staging directory rather than
overwriting the retained older fallback. Use `flip_deploy.py build` for runtime
updates; it hashes allowlisted files and does not transfer the ROM. Do not rerun
disc installation for each test. The launcher's `data/state/game.log` records
ordinary sessions. Select+Start exits; choose No at the inherited incomplete
memory-card creation prompt.

Use `flip_backend_trial.py` with a unique name for controlled scene tests. It
requires an idle game and MainUI for display handoff. Keep control and candidate
stage, seed, freeze point, clocks, capture policy, and soak duration identical.
The tool now stores device diagnostics on SD under `data/diagnostics/backend-trials`.
Earlier accumulation in RAM-backed `/tmp` consumed about 290 MiB and triggered
the memory guard; archived files were moved intact to
`data/diagnostics/game-barrier-pre-v70-moving`.

Local raw evidence is in `native/validation/2026-09-09-flip/staged-backend/`.
Use `analyze_flip_profile.py`, `analyze_flip_deep.py`, and `analyze_flip_cpu.py`.
The CPU sampler's x30 value is a return-address snapshot, not a full stack unwind.
Match samples to their archived binary (`build/flip-tools/perf-v63-symbols`,
`perf-v72-symbols`, or `perf-v73-symbols`), not a newly rebuilt executable.
The diagnostic executable is archived as
`build/flip-tools/melee_native-deep-profile-v76`.

## Next work, in priority order

1. Test the two v78 options independently against timers-OFF controls, then
   together only if each preserves frozen captures and moving gameplay. Neither
   candidate is accepted as a performance improvement yet.
2. Specialize common complete vertex formats or generate ARM64 vertex loaders
   to reduce repeated generic conversion and per-primitive setup.
3. Address temporary texture-object lifetime and pipeline configuration churn.
   Preserve mutable pixels, palettes, EFB copies, and memory reuse; replacing
   content validation with blind pointer caching is unsafe.
4. Add asynchronous GPU timing for draw groups to distinguish GPU shader cost
   from CPU submission overhead. Existing host GL-call timings do not do this.
5. Continue independent GLES work where measurements justify it. Native shader
   fidelity and speed need proof before removing the reference implementation.
6. Validate moving matches across stages, effects, menus, and longer sessions
   against the 16.67 ms budget before claiming solid 60 FPS.

Useful precedents are Dolphin's
[ARM64 vertex loaders](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/VertexLoaderARM64.cpp),
[loader cache](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/VertexLoaderManager.cpp),
[fenced GL streaming](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoBackends/OGL/OGLStreamBuffer.cpp),
and [hybrid ubershader design](https://ca.dolphin-emu.org/blog/2017/07/30/ubershaders/).
The native streaming ownership rules follow
[EXT_buffer_storage](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_buffer_storage.txt).
These are design references, not claims that Dolphin code was transplanted.

## Detailed reports

- [Initial port and validation](validation/2026-09-09-flip/REPORT.md)
- [Driver and conventional-vertex backend rewrite](validation/2026-09-09-flip/BACKEND_REWRITE.md)
- [Direct GLES iterations](validation/2026-09-09-flip/DIRECT_GLES_ITERATION.md)
- [Pipelined presentation and readback audit, v62–v70](validation/2026-09-09-flip/PIPELINED_PRESENT_ITERATION.md)
- [Native streaming and deep profiling, v71–v77](validation/2026-09-09-flip/DEEP_PROFILE_AND_STREAMING.md)

The earlier reports preserve experiments and their limitations. This handoff
supersedes their descriptions of the latest local/device state.
