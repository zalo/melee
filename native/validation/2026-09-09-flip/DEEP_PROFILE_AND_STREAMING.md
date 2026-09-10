# Native streaming and draw-pattern profiling, 2026-09-10

60 FPS remains unachieved. This iteration adds native GLES upload ownership
and deeper attribution of CPU costs to GX formats and draw patterns. Device:
RK3566/Mali-G52, Surwish, process-local g29p1. ROM data was not uploaded.

## Native persistent uploads

`MELEE_FLIP_STREAM_UPLOAD=1` opts into three sets of native vertex, index,
and uniform buffers. Each is persistently, coherently mapped using the
advertised `GL_EXT_buffer_storage` extension. A completion fence after queue
submission protects the entire set until reuse. No per-draw barrier or CPU
readback is introduced. Uniform windows are offsets within native uniform
storage, and the binding cache includes the window offset.

Only frames whose shared-geometry consumers are qualified resource-only GX
passes skip Dawn geometry uploads. Other frames use reference uploads, and
shadow caches are invalidated across those transitions. Allocation/extension
failure falls back before skipping uploads. Fence failure aborts before any
unsafe overwrite. Shutdown waits before releasing mapped storage. Resource
tracking, textures, ordinary uniforms, and non-GX passes still use Dawn.

`MELEE_FLIP_STREAM_DIRTY=0` copies every active byte. The default within the
experiment compares actual bytes against a shadow for each of the three sets
and copies coalesced changed ranges. `MELEE_FLIP_STREAM_EVERY=2` deliberately
alternates native and reference uploads to exercise fallback transitions.

The ownership follows the visibility and reuse requirements of
[EXT_buffer_storage](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_buffer_storage.txt).
The native implementation is independent code; Dolphin's fenced streaming
design was a reference, not transplanted source.

| Trial | Frozen scene | Last-120 mean ms | Median ms |
| --- | --- | ---: | ---: |
| v71-stream-control-onett | Onett / 35, normal uploads | 30.190 | 29.983 |
| v71-native-stream-onett | Onett / 35, full native copies | 30.022 | 29.742 |
| v72-native-stream-sample | Onett / 35, native changed ranges + CPU sampling | 30.284 | 30.031 |
| v72-stream-stage31 | Battlefield / 240, native changed ranges | 18.761 | 18.476 |

v70's frozen Battlefield comparison was 19.702 ms mean. The native path remains
opt-in: Onett has no convincing end-to-end gain yet. Full native copies move
3,845,980 bytes in about 1.95 ms; changed-range comparisons write only 16,692
bytes but still take about 2.2 ms. Slot reuse waits are approximately 0.02 ms.
The upload comparison itself appears prominently in CPU samples. Removing the
driver copy did not remove the CPU memory scan.

Both Onett EFB/scanout hashes match their references in v71/v72. Battlefield's
EFB and scanout match v70/v66 exactly. All marked kernel-log intervals from
v71 through v74 have no new fault/error/hang/reset lines. Matching unstripped
CPU-sampling binaries are retained as `build/flip-tools/perf-v72-symbols` and
`perf-v73-symbols`.

## Deeper CPU profiling

Build with CMake `-DMELEE_FLIP_DEEP_TIMERS=ON` to enable the detailed hooks.
`MELEE_FLIP_DEEP_PROFILE=1` then samples every 120th frame by default, adjustable via
`MELEE_FLIP_DEEP_INTERVAL`. Scopes cover FIFO processing, primitive preparation,
pipeline configuration, texture resolution/hashing/bind-group construction,
uniform construction, vertex formats and conversion, staging copies, uploads,
and separate GL pipeline/texture/uniform/vertex/draw operations.

Each rendered draw has a frame/pass/ordinal tag joined with pipeline identity,
GL program, vertex/index counts, decoded stride, TEV count, texture-group
identity, uniform window, and program/pipeline change flags. The analyzer can
therefore group timings by actual rendering pattern rather than only function.

The default v75 scopes use wall clocks. Optional `MELEE_FLIP_DEEP_CPU=1` also
reads thread CPU clocks. The v73/v74 CPU-clock traces substantially perturb
small scopes; their frame totals are **not** performance benchmarks. v75 removes
those clock syscalls and replaces string-keyed ordered maps with pointer-keyed
hash tables. Sampled frames still include observer cost. Inclusive parent and
child scopes overlap and must not be summed. Wall-minus-CPU, when measured,
can include scheduling as well as waits; it is not a GPU-duration measurement.

FIFO scopes follow the FIFO worker via a published frame ID. Initial v73 scopes
mistakenly started on the recording caller and contain only render data; v74
corrects that. Completed FIFO frames flush when the next frame reaches that
worker, so the final frame can be absent after shutdown.

The IP sampler now also captures x30. This supplies a return-address snapshot
useful for leaf memcpy/memcmp/hash attribution, **not** a guaranteed full stack
unwind. v73 samples attribute XXH3 directly to `hash_texture_source` and the
render worker's large libc comparison group to `FlipStream::upload`.

## v75 findings

The last three sampled frozen Onett frames (720/840/960) each contain 2,095
primitive preparations, 6,866 decoded attributes, and 350 actual GX GL draws.
There are 393 pipeline rebuilds, 471 uniform builds, and 366 texture hashes
covering 3,998,944 bytes per frame. Reference captures remain byte-identical.

Selected inclusive wall measurements per sampled frame:

| Work | ms |
| --- | ---: |
| Vertex decoding, including attribute conversion | 10.837 |
| Attribute conversion only, subset of decoding | 7.976 |
| Pipeline configuration/build | 3.903 |
| Texture hashing | 2.558 |
| Vertex staging copy | 1.268 |
| GL draw calls | 5.880 |
| GL texture binding | 2.431 |
| GL uniform-buffer binding | 0.099 |

The common unchanged-program draw groups take about 12–14 microseconds per GL
call; program-change groups often take 18–23 microseconds. One small-draw
program averages about 70 microseconds, but it is only two calls per frame.
Repeated ordinary submission costs dominate that isolated outlier.

The largest vertex-format groups are INDEX16 S16 XY texture coordinates,
INDEX16 F32 XYZ positions, and INDEX16 S16 NBT normals. NBT is more expensive
per vertex but less frequent. Counts and format identities are exact for the
trace; timing estimates remain subject to observer cost and scheduling.

The source also shows `HSD_TObjSetup` constructing temporary GX texture objects
and `GXInitTexObj` assigning a new identity each time. This explains a path to
repeated texture hashing and object-cache churn. A future lifetime fix must
preserve mutable textures, palettes, EFB copies, and pointer reuse; simply
trusting a texture address is insufficient.

## Reproduce analysis

```sh
python3 native/tools/analyze_flip_deep.py native/validation/2026-09-09-flip/staged-backend/v75-deep-wall-onett.log
python3 native/tools/analyze_flip_cpu.py native/validation/2026-09-09-flip/staged-backend/v73-deep-onett --binary build/flip-tools/perf-v73-symbols --callers
```

The ARM vertex decoder test passes with the profiling changes. The tracked
Aurora patch includes the native stream, nested profiler, and call-site hooks
and passes reverse-application checks. Default launcher settings remain v70;
the native stream and deep profiling are opt-in.

The untraced v75 control was 32.099 ms mean; v76's cheaper inactive scopes
reduced it to 31.071 ms, still above the v71 30.190 ms control. Consequently
v77 makes the hooks a compile-time option defaulting OFF: the production build
eliminates the scopes, metadata, and TLS access entirely. The v76 diagnostic
executable is retained at `build/flip-tools/melee_native-deep-profile-v76`.
Tracing does not qualify a change's speed: use a separate timers-OFF control.

`v77-timers-compiled-out` restores the baseline: 30.099 ms mean / 29.750 ms
median / 33.387 ms p95 on frozen Onett, with exact EFB and scanout reference
hashes and no new marked kernel faults. The installed executable is v77 with
`MELEE_FLIP_DEEP_TIMERS=OFF`; the diagnostic executable remains archived.
MainUI is running after testing, cores are back to 0-1, and CPU/GPU/DMC
governors are restored to ondemand/simple_ondemand/dmc_ondemand.

`v75-stream-fallback-moving` alternates native/reference uploads during moving
Battlefield gameplay, with a 20-second post-capture soak. It completes without
new kernel faults and shows the stage, fighters, effects, and HUD. The moving
capture is a different simulation tick from the v70 capture, so it is not used
for a byte-equality assertion. Its last-120 mean is 21.891 ms, median 21.788 ms;
this mixed diagnostic workload does not establish a production speedup. The
frozen v75 EFB/scanout captures remain exact reference matches. All 16 launcher
and deployment tests pass.
