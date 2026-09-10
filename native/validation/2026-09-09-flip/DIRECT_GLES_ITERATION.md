# Direct GLES optimization, 2026-09-10

Full speed is not achieved. This iteration improves actual-game rendering and
implements an independent specialized GLSL generator, but Dawn still owns the
resources, reference programs, command encoding, and presentation. The tested
device is the RK3566/Mali-G52 Flip V2 running Surwish with process-local g29p1.
The ROM was never transferred during these trials. The older dist package is
retained as a fallback.

## Qualified changes

- Correct GLES front-face winding: GX's WebGPU CW convention becomes GL_CCW
  after the vertex shader's clip-space Y inversion. This was the cause of the
  missing surfaces in earlier direct replay captures. Previous timings of
  incomplete direct rendering are not a valid performance baseline.
- Cache pipeline metadata, resource-to-texture-unit mappings, uniform bindings,
  and texture/sampler state. Reset mutable GL caches at pass boundaries; account
  for texture-view parameter aliasing between units. Use GLES 3.1 separate
  vertex-format/buffer binding to update the VBO offset once per draw.
- Preserve unsupported passes on the reference path, including MSAA, multiple
  color targets, fog-range/immediate variants, and polygon-offset clamp.
- Upload ordinary resolve/conversion/custom uniform ranges separately from GX
  windows, avoiding the duplicate upload of all GX uniforms to the old buffer.
- NEON conversion for common signed/unsigned 16-bit XY/XYZ attributes. The
  six-byte XYZ tail is read without overreading. Device differential tests
  compare against the scalar reference decoder.
- Optional asynchronous, nonblocking GPU elapsed-time queries alongside direct
  submission wall timing. Disjoint samples are discarded. These measure a GPU
  pass interval, which can include GPU idle gaps between CPU submissions.
- Launcher defaults select direct submission on g29, asynchronous presentation,
  and fast validation. Explicit barriers default to reference submission. CPU
  and GPU performance governors and enabled cores are restored on exit.

## Native GLSL experiment

`MELEE_FLIP_NATIVE_SPECIALIZED=1` keeps GX material state in the shader key and
generates constant TEV, texgen, lighting, alpha-test, and fog state. TEV stages
are explicitly unrolled so the driver can discard unused code and samplers.
Normal-attribute presence is now included in the packed state. Only active
samplers are bound. Programs have an on-disk binary cache keyed by vertex and
fragment source plus vendor, renderer, GL, and GLSL version. Failed/incompatible
binary loads fall back to source compilation; writes use atomic replacement.
`v61-native-binary-cold` compiles 87 native programs; the matching warm trial
loads all 87 from the binary cache with no GL/compile errors. Their captures
are byte-identical to each other (the native/reference differences remain).

This path is **not the default**: its Onett capture has small differences from
the reference and its GPU pass remains slower. The old universal native shader
was approximately 114 ms/frame; constant material specialization and active
sampler binding reduced the measured median to roughly 42 ms, compared with
roughly 35 ms for direct replay of the existing specialized programs. This is
evidence for specialization, not evidence that the native generator is complete.

## Controlled measurements

Raw evidence is in `staged-backend/`. CPU performance governor reports
1,992 MHz and GPU performance governor reports 900 MHz. The table uses the
complete last 120 `[flip-profile]` observations from each named trial. Diagnostic
readback, PPM writes, logging, and timer queries remain included; these are
instrumented scene measurements, not a claim about every match or stage.

| Trial | Scene / freeze tick | Mean frame ms | Median frame ms |
| --- | --- | ---: | ---: |
| v61-dawn-onett-control | Onett / 35 | 49.59 | 45.71 |
| v61-direct-onett | Onett / 35 | 38.63 | 34.62 |
| v61-dawn-stage31 | Battlefield / 240 | 29.95 | 29.73 |
| v61-direct-stage31 | Battlefield / 240 | 24.52 | 24.38 |

The Onett control disables direct submission, sparse uniforms, and NEON; both
trials use the same clocks, asynchronous presentation, and fast validation.
The Battlefield comparison changes submission only, retaining NEON/sparse
uploads in both trials. Battlefield runs through gameplay before freezing.

The cached versus uncached direct control (`v58-direct-cache` versus
`v58-direct-uncached`) saves about 1.4 ms in the last-120 mean. Changes in clocks
must not be misattributed to that cache. Texture-pair batching
(`v60-direct-neon-pairs`) changes the reference image and does not improve this
window's timing, so remains disabled.

## Image and fault validation

The complete EFB captures are byte-identical between direct and reference:

- Onett: `6f1c16fb5606542b8c55764b9e5663ea81c591f5a38458d41222213c399b31eb`.
- Battlefield: `79339b51acb2f8673e73c770adde523b5e21c4fe42f58bd590d84c5f38d1545f`.

v56 reference/direct scanout captures also match exactly. Per-trial `/dev/kmsg`
markers delimit fault checks; the completed v56–v61 direct/reference tests have
no new GPU fault lines. Every reported capture trial completed under its
watchdog. These checks do not qualify every GX feature or prolonged gameplay.

The installed launcher was also exercised by `v61-launcher-moving-onett`, with
no simulation freeze and a 20-second soak after its first capture. It completes
with no new GL errors or kernel GPU faults; the last-120 mean is 37.97 ms and
median 34.35 ms. This moving scene is not an identical-workload comparison to
the frozen table. After exit, MainUI is running and the device's original
`ondemand` CPU governor, `simple_ondemand` GPU governor, and online cores `0-1`
are restored. The prior launcher is retained on-device as `launch.sh.pre-v61`.

The ARM `melee_flip_vertex_test` passes on the device. Ten launcher tests cover
driver selection, barrier overrides, core restoration, and governor restoration
on normal exit, failure, and termination. Tracked Aurora/Dawn patches reproduce
the dependency source changes and pass reverse-application checks.

## Remaining work toward 16.7 ms

The `v59-direct-sparse-sample` CPU profile records 8,728 samples on the render
worker and 8,638 on the FIFO worker, versus 1,992 on the game thread. The profile
includes capture work. FIFO costs include packed geometry decoding, texture
hashing, uniform construction, and draw preparation. Render costs include the
Mali driver plus remaining Dawn resource tracking and command encoding.
The matching unstripped sampling executable is retained at
`build/flip-tools/perf-v59-symbols`; do not symbolize those samples against a
newer executable.

The largest sampled Onett direct pass takes about 10 ms of CPU submission and
10 ms of GPU elapsed time at fixed clocks. The native GLSL variant reduces
submission to about 11 ms after unused sampler removal but uses about 14 ms of
GPU elapsed time. These intervals overlap and must not simply be added.

Next architectural work is to remove duplicated Dawn command/resource tracking,
retain geometry/material work with explicit invalidation, and reduce native
shader cost while broadening GX correctness. Reordering draws without proving
blend/depth/EFB dependencies safe is not an accepted optimization.
