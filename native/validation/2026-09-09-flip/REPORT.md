# Miyoo Flip V2 validation — 2026-09-09

> Latest result: both stages are deployed. See [the completed backend rewrite and validation](BACKEND_REWRITE.md). Earlier driver defaults, interim failures and proposed work below describe historical investigation.

## Device and build

- Device: 10.0.0.178, passwordless ADB, Linux AArch64.
- Firmware: Buildroot 2021.11 identification, kernel 5.10.160, glibc 2.36.
- GPU: Mali-G52; ARM g13p0-01eac0; EGL 1.4, OpenGL ES 3.2.
- Display: 640×480, DRM connector 179 / CRTC 107 (discovered at runtime).
- Memory: 956 MiB reported; no swap during the GPU probe.
- Build: Clang 22.1.8, Bootlin GCC 12.3 SDK, Release, Cortex-A55.
- Aurora: 749d6ee7a22bdfab78c8ece9047bca5d79aa72ca plus tracked Flip patch.
- Dawn: 1155e0ed531126f33a1279afa029349651ca1c93 plus tracked EGL native-window patch.
- Rust/nod: Rust 1.98.1, nod v2.0.0-alpha.10 built for aarch64-unknown-linux-gnu.

## Confirmed

- Full game executable cross-compiles and starts on the actual device.
- Runtime needs at most GLIBC_2.34; bundled libstdc++ needs at most GLIBC_2.36.
- Stock driver exposes **zero vertex-stage SSBOs**; the Flip texture-fetch path
  requests zero and uses two unsigned integer textures instead.
- Dawn creates a hardware OpenGL ES device and EGL/GBM presentation surface.
- Seven 60-frame GPU probe phases pass: direct, indexed, textured/depth,
  big-endian, transformed, two transformed direct draws, and two transformed
  indexed draws. Readbacks have 197 distinct sampled colors and the expected
  positions; indexed vertex data crosses a 4096-byte texture row boundary.
- Multi-draw rendering requires a texture-fetch barrier on this stock driver.
  Without it, kernel logs report DATA_INVALID_FAULT and the image freezes.
- The optimized ARM ARQ regression passes on the device; synchronized completion
  checks fix the animation-loading spin seen in release builds.
- Native 4×4 projection storage fixes a stack overwrite in WorldToScreen;
  three other matching projection allocations were corrected as well.
- SDL recognizes the internal controller as Xbox 360 (VID 045e/PID 028e).
- Four deployment regression cases pass: skip identical ROM, reject replacement
  ROM, reject ROM in runtime bundle, and reject a failed transfer checksum.
- CTest native_disc and native_flip_deploy pass on the build host.
- A fresh package passes ELF architecture/glibc checks. A repeat deployment
  skips unchanged executable/probe files; final [bundle hashes](bundle-sha256.json)
  identify the tested runtime.
- The final packaged binary passes all seven GPU phases without preload helpers
  ([log](gpu-probe.log)). The Ports-menu shim starts the normal launcher with
  profiling disabled. Normal SIGTERM shutdown returns to MainUI.
- Repeated SDK preparation validates that both tracked dependency patches are
  already applied cleanly. Host Python checks and `git diff --check` pass.

![Hardware GPU probe readback](gpu-probe.png)

## Disc

The replacement RVZ passes nod's full read-through verification and matches
Redump Melee US revision 2:

- Game ID: GALE01, revision 2.
- Logical-disc SHA-1: d4e70c064cc714ba8400a849cf299dbd1aa326fc.
- CRC32: 5365c84b; XXH64: 37a7b606107d9bd6.
- Compressed RVZ size: 1,133,079,700 bytes.

The two earlier incomplete files were rejected locally and not uploaded.
The verified replacement is installed once in the persistent data directory;
subsequent runtime deployments do not include disc data.

## Gameplay

The device renders a CPU Fox/Mario match on Onett with stage geometry,
fighters, HUD, effects and music. The user confirmed that graphics, audio and
navigation work. The capture below was read from the actual DRM scanout.
Several short match runs passed the former loading/stack-crash points. This
is not sustained full-speed or exhaustive stage/character coverage.

![Actual Onett match scanout](match-working.png)

### Frame timing

The optional `MELEE_FLIP_PROFILE=1` profiler records CPU wall times on the
render thread. It does not provide GPU timestamps. Draw and barrier times
are subsets of submission time. Scripted scene transitions restrict these
measurements to gameplay; launch and shader-cache warm-up are excluded from
that scene filter, though early gameplay frames can still contain compilation.

The [baseline CSV](profile-baseline.csv) and [summary](profile-baseline.json)
contain 70 match frames:

| Measurement | Mean milliseconds |
| --- | ---: |
| Presented frame interval | 309.588 |
| Graphics-driver submission | 279.312 |
| Draw calls (within submission) | 108.762 |
| Texture-fetch barriers (within submission) | 124.259 |
| EGL swap | 3.050 |
| GBM front-buffer acquisition | 0.001 |
| DRM page flip | 9.186 |

Graphics submission consumes about 90% of the frame interval. The match runs
around 3–4 FPS, with roughly 450 draw calls per frame. Display scanout is not
the primary bottleneck. The profiler is disabled in the normal launcher.

A [localized barrier experiment](profile-region.json), with [204 frame samples](profile-region.csv),
averaged 293.334 ms/frame and 267.711 ms/submission. It remained similarly slow;
the samples cover different amounts of gameplay, so this is not evidence of
a reliable speedup. The production build retains the ordinary texture-fetch
barrier. The seven-phase probe also rejected uniform-only, framebuffer-only,
upload-only barriers and an explicit vertex-ID attribute without per-draw
barriers. Replacing single-instance draws with non-instanced calls also fails
the multi-draw probe. `glFlush` fails; `glFinish` works but is expensive. No GPU overclock or
firmware replacement is included. The temporary GPU governor test was restored
to `simple_ondemand`.

Reduced Flip staging/frame pools lower reserved pool memory by about 265 MiB.
ROM data is reused for every test; no subsequent runtime upload includes it.

## Scope

The existing Mac/Linux stage/character/item matrix is not Flip coverage.
The GPU probe is a renderer test, not gameplay acceptance. Manual physical
button labels, analog calibration, battery behavior and sustained gameplay
performance still need device testing. Saving is an inherited incomplete
feature. No firmware or system graphics libraries have been replaced.

## Follow-up threading and longer-run tests

See [threading experiments and native-port research](THREADING.md). Four-core
samples improved throughput, but longer runs exposed memory pressure, including
an OOM kill and a device stall. The earlier short gameplay checks do not certify
sustained stability. The follow-up report tracks candidate mitigation status.

## Runtime barrier isolation

The current bundle includes live barrier configuration, with full barriers by
default. [Real-game and live isolation results](BARRIERS.md) supersede any
inference from the synthetic probe about which gameplay draws need barriers.
Random mask tests failed; a live single-removal test isolated faults after draw
32 in one paused scene. Several other individual removals matched adjacent
controls, but no general reduced-barrier policy is validated. The current
bundle hashes are recorded in `bundle-sha256.json`.

## Barrier root-cause follow-up

See [ROOT_CAUSE.md](ROOT_CAUSE.md) for the direct GLES reproduction, corrected
kernel-marker sweep, host-driver controls, and alternative renderer prototypes.
The fixed-scene removal mask failed moving Onett and is not a normal default.
The current bundle includes GPU-draining diagnostic policy transitions,
opt-in simulation freeze, and one-frame state tracing; the normal render
policy remains conservative. The ROM was not transferred.

## Surwish identification, Mali precedents and newer drivers

The user identifies the installed OS as Surwish. Read the updated
[Mali pipeline investigation](MALI_PIPELINE.md): g24p0 and g29p1 were tested
app-locally. g29p1 fixes the tiny no-barrier control but still needs barriers
to display the real game. Its full-barrier scene is visible but differs from
the earlier reference, so performance results remain provisional. Default
launcher and firmware files are unchanged.
