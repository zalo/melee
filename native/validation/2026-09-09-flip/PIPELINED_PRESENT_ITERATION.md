# Pipelined presentation and readback audit, 2026-09-10

The 60 FPS goal remains open. These measurements use the RK3566/Mali-G52
Flip V2, Surwish, and process-local g29p1. The ROM was not transferred.
The older `dist/flip/Melee-Native-Flip` package remains a fallback.

## Changes since v61

- `MELEE_FLIP_DIRECT_PACKET=1` records resource references for supported GX
  passes without encoding redundant Dawn draw commands. Unsupported passes
  retain the reference path. Direct plans match pass labels and keep resources
  alive and tracked. Dawn still owns resource allocation and queue submission.
- `MELEE_FLIP_DIRECT_CHECKS=0` removes per-draw GL error queries while retaining
  pass checks. This flag is independent of WebGPU fast validation.
- `MELEE_FLIP_CAPTURE_ONCE=1` takes one diagnostic screenshot. Earlier trials
  periodically read back and rewrite the PPM; their timing is not directly
  comparable to these trials. Normal gameplay has no diagnostic screenshot
  unless the render-check/matrix-test hook is enabled.
- Numeric vertex conversion selects fixed component counts before its inner
  loops. Differential tests cover XY, XYZ, single-component texture coordinates,
  endian conversion, indexed/direct attributes, and short array tails.
- The unused second texture bank is omitted unless texture-pair batching is
  explicitly selected. This saves dummy resource tracking but gave no clear
  isolated frame-time gain.
- `MELEE_FLIP_MAP_UPLOAD=1` maps full-buffer writes with invalidation, without
  unsynchronized writes. It remains experimental: measured gains were negligible.
- `MELEE_FLIP_PRESENT_THREAD=1` moves EGL swap and DRM/GBM presentation onto a
  shared-context worker with bounded queuing. Producer and consumer fences
  protect transferred textures. Shutdown drains and joins before destroying
  the EGL surface. Unsupported context creation falls back to synchronous
  presentation. Both contexts use matching version and robustness settings.
- The launcher can select the supported DMC performance governor alongside
  CPU/GPU governors, restoring all three on exit. Firmware selected 780 MHz
  DMC in these Onett tests; performance selects 1056 MHz. This costs more power.
- `MELEE_FLIP_DIRTY_UPLOAD=1` compares actual geometry/index/uniform bytes with
  shadow copies and uploads coalesced changed ranges. External encoder tasks
  and custom draws invalidate the shadows. Tests cover edits, untouched frames,
  shrinking/growing buffers, and invalidation. Textures and ordinary uniforms
  retain their existing upload paths.
- v70 adds profiling-only cumulative GL readback counters for `glReadPixels`
  and read-mode `glMapBufferRange`. Write maps are excluded. Reported time is
  CPU wall time in the calls, not asynchronous GPU transfer duration.

## Controlled frozen-scene measurements

All values are the last 120 observations from the named logs in
`staged-backend/`. Onett freezes at tick 35, screenshot at frame 60. CPU/GPU
performance governors are selected. DMC uses firmware policy unless stated.
Threaded presentation uses `[flip-thread-present]` frame intervals, not the
renderer submission interval. Overlapping CPU and presentation times must not
be added. These are instrumented scene tests, not broad gameplay benchmarks.

| Trial | Change | Mean frame ms | Median frame ms |
| --- | --- | ---: | ---: |
| v62-replay-onett | Direct replay, capture once | 38.271 | — |
| v62-packet-onett | Resource-only encoding | 37.372 | — |
| v66-dmc-control | Current synchronous control | 37.337 | — |
| v66-dmc-performance | DMC performance only | 33.951 | — |
| v68-present-thread | Presentation worker only | 31.280 | 31.279 |
| v69-dirty-upload | Changed upload ranges only | 35.913 | 35.509 |
| v69-combined | DMC + worker + changed ranges | 30.141 | 29.924 |
| v70-readback-cache | Combined + vertex cache | 29.944 | 29.769 |
| v70-readback-stage31 | Combined, Battlefield / 240 | 19.702 | 19.508 |

The combined trial's p95 is 32.867 ms. Renderer submission averages 16.746 ms
wall / 16.605 ms CPU. Presentation takes 10.608 ms on its worker; those
intervals overlap. The frame interval is still about 33 FPS, above the 16.67 ms
budget. `v69-combined-cache` accidentally uses the unrecognized
`MELEE_FLIP_GEOMETRY_CACHE` name and is only a repeated combined trial;
it does **not** test the actual `MELEE_FLIP_VERTEX_CACHE` setting.

In the frozen dirty-upload trial, late frames compare 3,845,980 bytes and write
16,692 bytes in 40 ranges. Avoiding more than 99% of those upload bytes improves
frame time only modestly. Transfer bandwidth alone is not the dominant cost.

v68-present-thread and v69-combined have byte-identical EFB and physical
scanout captures to the Onett references. Their marked kernel-log intervals,
and v69-dirty-upload's interval, contain no fault/error/hang/reset lines.
The initial v67 shared-context attempt failed with EGL_BAD_MATCH; v68 matches
producer context attributes and provides clean fallback on creation failure.

## CPU evidence and readback audit

`v63-packet-sample` retains matching symbols at
`build/flip-tools/perf-v63-symbols`. FIFO/render threads dominate samples over
the game thread. FIFO costs include vertex conversion, content hashing, uniform
construction, and draw preparation. The frequently sampled Mali routine is a
NEON memory-copy loop, not a spin loop; samples alone do not identify its source
buffers or establish GPU readback.

The optional depth snapshot path returns early when WebGPU core features are
absent, as in this compatibility build. No Melee/HSD call sites of `GXPeekZ`
were found. Tracy timestamp readback is compiled out (`TRACY_ENABLE=OFF`).
The CPU upload staging path is host memory; write mapping is not readback.
GPU texture copies and presentation fence waits are also distinct from CPU
image downloads.

`v70-readback-cache` confirms exactly one `glReadPixels` call (307,200 pixels)
and one read mapping (1,228,800 bytes) for the screenshot. Their cumulative
CPU call times are 0.541 ms and 8.295 ms respectively. Counts remain unchanged
from submission frame 600 through 1080. Thus no recurring explicit CPU readback
occurs in that steady-state Onett window. Its final-120 mean is 29.944 ms,
median 29.769 ms, with both reference hashes unchanged. The actual vertex cache
has over 352,000 hits and 648 misses by the end, but its frame-time difference
from v69-combined is too small to justify enabling it by default.

`v70-readback-stage31` also records one screenshot: 0.573 ms in `glReadPixels`
and 5.822 ms in the read mapping. Counts remain unchanged from frame 840 through
1440. Its EFB hash matches the Battlefield reference exactly. Both v70 frozen
trials have no new fault/error/hang/reset lines after their kernel markers.
Battlefield's physical scanout also exactly matches v66-default-stage31.

The g29 direct-mode launcher defaults now select resource-only encoding,
per-pass rather than per-draw GL error checks, threaded presentation, and changed
uploads. Explicit overrides remain respected; g13 keeps its original fallback.
The launcher additionally boosts supported memory clocks and restores them.
Sixteen launcher/deployment tests pass, including override preservation and
governor/core restoration on normal exit, game failure, and termination.

The first `v70-launcher-moving-onett` attempt stopped at the 180,000 KiB memory
guard before capture. `/tmp` is tmpfs and had accumulated 290.2 MiB of trial
evidence. That directory was preserved by moving it to the SD card at
`data/diagnostics/game-barrier-pre-v70-moving`. The trial runner now stores
future captures/logs under `data/diagnostics/backend-trials` on the SD card and
rejects names with existing local evidence. This avoids charging retained test
artifacts against the game's RAM budget. The rerun is recorded separately.

`v70-launcher-moving-onett-clean` completes its 25-second post-capture soak
using the deployed launcher defaults, without a simulation freeze. It reports
35.683 ms mean / 32.057 ms median / 39.931 ms p95 over the last 120 frames.
This changing workload is not directly comparable to frozen Onett. The EFB
capture shows the textured stage, both fighters, shield/hit effects, and HUD.
Readback counters remain at one screenshot through submission frame 1440;
there are no new marked kernel faults. Available memory remains about 448 MiB
(458,264 KiB in the final sample).
The deployed launcher backup is `launch.sh.pre-v70`; the ROM was untouched.

`v70-launcher-moving-stage31` exercises the updated SD-card trial path and the
deployed defaults for a 20-second post-capture soak. Its last-120 mean is
24.882 ms, median 24.526 ms, and p95 29.758 ms. The capture shows both fighters,
the stage and platforms, effects, and HUD. Explicit readback remains one capture
through frame 1440, and the marked kernel interval contains no new faults.
These moving-scene rates are approximately 28 FPS on Onett and 40 FPS on
Battlefield; neither is full speed. After the final trial, MainUI is running,
the game has exited, online cores are restored to 0-1, and CPU/GPU/DMC governors
are restored to ondemand/simple_ondemand/dmc_ondemand. Both dependency patches
pass reverse-application checks, the ARM build succeeds, and all 16 launcher
and deployment tests pass.

## Applicable Dolphin precedents

- [ARM64 vertex loaders](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/VertexLoaderARM64.cpp)
  generate a decoder for a fixed vertex format. Our existing template loops
  specialize scalar conversion but retain dynamic addressing and layout work.
  Further specialization is a strong candidate given the FIFO CPU samples.
- [OpenGL stream buffers](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoBackends/OGL/OGLStreamBuffer.cpp)
  implement fenced rings, map/orphan fallbacks, and persistent mapping where
  supported. Synchronization protects storage reuse rather than every draw.
  The saved `mali-precedents/g29-egl-caps.log` advertises
  `GL_EXT_buffer_storage`; persistent mapping is a candidate for a separate
  correctness/performance probe, not yet qualified by those extension strings.
- [Hybrid shaders](https://ca.dolphin-emu.org/blog/2017/07/30/ubershaders/)
  use specialized programs when available and an ubershader while compilation
  runs in the background. Our independent native specialized generator remains
  opt-in because it is slower and still differs slightly from reference output.
- [EFB peek caching](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/FramebufferManager.cpp)
  caches readback tiles and populates missing data on demand. This matters if a
  CPU EFB consumer is active; the current source audit does not show one here.

These are design references, not claims that Dolphin's code can be transplanted
unchanged or that any one technique guarantees 60 FPS on this device.
