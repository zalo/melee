# Miyoo Flip on the upstream Aurora PR set (`miyoo-flip-aurora-prs`)

This branch builds the Flip port against **upstream-style Aurora** instead of the
Flip-specific Aurora and Dawn patches that the `miyoo-flip` branch (v143) carries.
Its purpose is to measure, on the device, what the nine upstream-candidate Aurora
performance branches buy by themselves, without the direct GLES escape hatch, the
presentation worker, the Dawn framebuffer cache and swapchain pool, mapped GL
streams, scene-on-surface, or half-resolution sprites. Nothing in this document
has been run on the Flip yet; the device was offline when the branch was built.

## What the branch carries

### Aurora

`native/tools/bootstrap.py` pins Aurora to upstream `main`
`d0c931da2ed3f41d0e42736c2ab52a78c7cf1a9d` (`encounter/aurora`, "Handle
CP_CMD_INVAL_VTX"). `native/platform/flip/aurora-flip.patch` is now
`git diff d0c931d integration/flip-prs-platform` from the local Aurora clone
(`~/Desktop/aurora`, worktree `~/Desktop/aurora-wt/integration-flip-prs-platform`),
verified to apply to a pristine `d0c931d` checkout with `git apply --check`. That
branch is `integration/flip-prs` (all nine perf branches merged, `1c6deab`) plus
one platform commit (`f1650fd`).

The nine upstream-candidate branches (descriptions in `~/Desktop/aurora-prs/perf-*.md`):

| Branch | AuroraConfig toggle | What it does |
| --- | --- | --- |
| `perf/skip-empty-imgui-pass` | none (always) | no ImGui render pass when the overlay has no draws |
| `perf/gx-stable-texture-identity` | `textureVerifyInterval` | texture identity from the image description; sampled content re-hash every N frames |
| `perf/gx-pipeline-state-memo` | none (always) | raw-state hash reuses pipeline config / shader info |
| `perf/async-frames` | `asyncFrames` | frame markers in the GX stream; game thread never joins the translator; `GXWaitDrawDone` provided by Aurora; FIFO buffer compaction |
| `perf/gfx-texture-arrays` | arrays always; `textureAtlas` | GX textures as layers of shared array textures; clamp-wrapped single-mip textures share atlas layers (atlas is not bit-exact) |
| `perf/gfx-pass-fusion` | `disableRenderPassFusion` | second shadow map recorded into the first shadow pass; two-target conversion pass |
| `perf/gx-vertex-loaders` | `cpuVertexDecode` | specialized CPU vertex decoders into conventional vertex inputs; line/point expansion on the CPU |
| `perf/gx-resident-display-lists` | `residentDisplayLists`, `residentGeometryBudget` | display lists decoded once into GPU arenas; `GXInvalidateResidentGeometry()` releases them |
| `perf/gx-instanced-point-sprites` | none (within `cpuVertexDecode`) | one record per GX point, corner from the vertex index |

GX_AURORA subcommands are Aurora's own now: `0x0042/0x0043` frame markers,
`0x0044/0x0045` `CALL_DL` / `INVALIDATE_RESIDENT`, all emitted inside Aurora.

The platform commit adds the non-performance hunks the device needs, judged file
by file from the old `aurora-flip.patch`:

| Kept (platform enablement) | Why |
| --- | --- |
| `lib/dawn/BackendBinding.cpp` | surface descriptor = `DawnSurfaceSourceEGLNativeWindow` with the GBM surface from `MeleeFlipNativeWindow()`; SDL has no video driver on the Flip |
| `lib/webgpu/gpu.cpp` | chain `RequestAdapterOptionsGetGLProc` (EGL display + `MeleeFlipEGLProc`) so Dawn's GL backend uses the GBM EGL display; prefer `PresentMode::Immediate` (DRM page flips pace the panel); `maxStorageBuffersInVertexStage = 0` because every Mali-G52 GLES driver measured (g13p0, g24p0, g29p1) exposes zero vertex-stage storage blocks and Dawn rejects a device request above the adapter limit; `maxUniformBufferBindingSize` from the adapter |
| `lib/gfx/frame.cpp` (new, not `#ifdef`) | with `cpuVertexDecode` the static bind-group layout declares the vertex/storage buffers fragment-only, so Dawn can create it under a vertex-stage storage limit of 0 (Dawn validates per-layout counts, `BindingInfo.cpp`); the CPU-decode WGSL never reads `vbuf`/`abuf` in the vertex stage |
| `lib/gfx/resources.hpp`, `lib/gfx/frame.hpp` | bounded pools for the 1 GiB device: 8/12/2/4/12 MiB uniform/vertex/index/storage/texture-upload, three staging buffers instead of five (present since the first Flip build; the 12 MiB vertex pool holds decoded float records) |
| `lib/card/CardGciFolder.cpp/.hpp`, `lib/dolphin/card.cpp` | memory-card fixes from v146-v149 (NOFILE results, bounded name compares, EXIST on duplicate create, delete/rename, quieter logs) |
| `lib/dolphin/gx/GXManage.cpp` | already in the PR set (`mark_draw_done` in `GXSetDrawDone`, `GXWaitDrawDone`) |

| Dropped from the old patch | Why |
| --- | --- |
| `lib/gfx/flip_*`, `lib/gx/flip_*`, `cmake/aurora_gx.cmake` | direct GLES submission, mapped streams, uber shaders, sprites: Flip-only performance paths |
| `lib/aurora.cpp` | Flip present/profile hooks, scene-on-surface, direct-present, `aurora_fifo_*` exports; async frames now come from the PR |
| `lib/gx/*.cpp`, `lib/gfx/*.cpp` renderer hunks | integer-texture vertex path, uniform tables, batching, FBO/bind-group caches, pipeline wait accounting: replaced by or outside the PR set |
| `lib/dolphin/gx/GXDispList.cpp`, `GXTexture.cpp`, `GXAurora.h` | `CALL_DL` and stable identities are in the PRs (config-driven, not env-driven) |
| `lib/webgpu/gpu.cpp` validation toggles | upstream Release keeps `skip_validation` + `disable_robustness`; the old default (validation on unless `MELEE_FLIP_FAST_VALIDATION=1`) was a diagnostic and the launcher shipped with it set to 1; robustness off measured no difference |
| `lib/webgpu/gpu.cpp` surface usage bits | only scene-on-surface needed `TextureBinding|CopySrc|CopyDst` on the swapchain |
| `lib/gx/texture.hpp` (`ObjectCacheIdleFrames` 60) | object churn is fixed at the source by stable texture identities |
| `lib/imgui.*` `has_draws` | in `perf/skip-empty-imgui-pass` |

Aurora builds and passes its tests on the host with the platform commit:

```
/home/agent-untrusted/Desktop/aurora-build.sh ~/Desktop/aurora-wt/integration-flip-prs-platform <build>
build rc=0
ctest: 100% tests passed out of 356
```

### Dawn

Dawn stays at `encounter/dawn` `1155e0ed531126f33a1279afa029349651ca1c93` (the
ref Aurora `d0c931d` pins). `native/platform/flip/dawn-egl-native-window.patch`
now contains **only the EGL-native-window surface source**: the `dawn.json`
chained struct `dawn surface source EGL native window` with SType `1000`,
`Surface.cpp/.h` (`Surface::Type::EGLNativeWindow`, `GetEGLNativeWindow()`), and
the `SwapChainEGL.cpp` hunk that calls `eglCreateWindowSurface` on that window.
Pristine Dawn creates EGL window surfaces only for Xlib, Wayland, Android and
Metal-layer sources, so without this hunk the device cannot present at all;
this is the one Dawn change that has to stay. Everything else in the old patch
was removed: the direct-GL escape hatch (`OpenGLBackend.h`, `OpenGLBackend.cpp`,
`CommandBufferGL.cpp`, `QueueGL.cpp`), presenter callbacks, swapchain texture
pool, `ReleaseHandleForPresent` / `FlipForgetFramebuffers` (`TextureGL.*`), the
FBO cache, `TryDirectGLRenderPass`, the timing instrumentation and
`MELEE_FLIP_MAP_UPLOAD`. The trimmed patch applies to the pristine archive
(`git apply --check` with `GIT_CEILING_DIRECTORIES`, as `prepare_flip.py` does)
and Dawn builds with `prepare_flip.py`'s cmake invocation.

### Native side

Everything that depended on the escape hatch or the Flip-only Aurora exports is gone:

- `native/platform/flip/present_worker.cpp` deleted. `display.cpp` keeps the
  DRM/GBM/EGL setup and the page flip; it wraps `eglSwapBuffers` through the
  EGL function loader Dawn already goes through (`MeleeFlipEGLProc`), so the
  scan-out (`MeleeFlipPresent`) runs right after Dawn's standard EGL swapchain
  swaps, on the render worker, exactly as the builds before v67 did. The
  per-draw texture-fetch barrier interception, GL state cache and readback
  profilers (`MELEE_FLIP_BARRIER_*`, `MELEE_FLIP_GL_STATE_CACHE`,
  `[flip-readback]`) are removed with it. `MELEE_FLIP_ASYNC_PRESENT=1` (a
  display-side option) still defers the page-flip wait to the next present;
  the default is the synchronous wait.
- `gx_bridge.cpp`: `GXWaitDrawDone` deleted (Aurora provides it);
  `MeleeNativeUnregisterVertexBuffer` calls `GXInvalidateResidentGeometry(ptr, size)`;
  no hand-written `GX_AURORA_*` records.
- `vi_runtime.cpp`: `[perf]` and `[pacing]` unchanged; `[perf-breakdown]`
  keeps the game-thread fields (`game_ms end_ms sleep_ms begin_ms`) and drops
  the worker busy times that came from `aurora_fifo_*` / render-worker exports.
- `render_check.cpp`: schedules the readback through `fifo::run_after_frame`
  when `aurora::g_config.asyncFrames` is set.
- `runtime_main.cpp` sets the renderer options in `AuroraConfig` (v143 defaults)
  and prints them as `[flip-config] ...` at start-up. Each has an override:

| AuroraConfig field | Default | Override |
| --- | --- | --- |
| `cpuVertexDecode` | on | `MELEE_FLIP_CPU_VERTEX_DECODE=0` (expected to fail at renderer init on Mali: zero vertex-stage storage buffers) |
| `residentDisplayLists` | on | `MELEE_FLIP_RESIDENT_DL=0` |
| `residentGeometryBudget` | 64 MiB | `MELEE_FLIP_RESIDENT_MB=N` |
| `asyncFrames` | on | `MELEE_FLIP_ASYNC_FIFO=0` |
| `textureVerifyInterval` | 4 | `MELEE_FLIP_TEXTURE_VERIFY_INTERVAL=N` (1 = every bind) |
| `textureAtlas` | on | `MELEE_FLIP_TEXTURE_ATLAS=0` (restores bit exactness) |
| `disableRenderPassFusion` | off (fusion on) | `MELEE_FLIP_FUSE_PASSES=0` |

- `launch.sh` no longer exports any renderer flag (`MELEE_FLIP_DIRECT_GLES`,
  `BARRIER_EVERY`, `BATCH_DRAWS`, `PRESENT_THREAD`, `DIRTY_UPLOAD`,
  `ASYNC_PRESENT`, `FAST_VALIDATION`, `ASYNC_FIFO`, `VERTEX_INPUT`,
  `UNIFORM_TABLE`, `DIRECT_PACKET`, `DIRECT_CHECKS`); it keeps the g29/g13
  driver selection, the governor/core handling and the cache/config/state
  paths. `tests/test_flip_launcher.py` asserts that none of the retired flags
  is exported. The g13 fallback is untested on this branch: without the
  per-draw barrier the installed driver is expected to lose geometry.
- `native/CMakeLists.txt`: Aurora revision check `d0c931d...`, no
  `MELEE_FLIP_DEEP_TIMERS`, no `present_worker.cpp`, `melee_flip_vertex_test`
  removed with `tests/flip_vertex_test.cpp` (it tested the removed
  `flip_vertex.hpp`).

Diagnostics still present on this branch: `MELEE_FLIP_PROFILE` (`[perf-breakdown]`
and `[flip-present] frame_ms lock_ms drm_ms`), `MELEE_FLIP_CAPTURE_ONCE`,
`MELEE_FLIP_RELEASE_TEXOBJ`, `MELEE_FLIP_SWAP_CONTROLS`.

## Build

Reuses the main checkout's SDK, Rust and Mali driver; only Dawn and Aurora are rebuilt:

```sh
MAIN=/home/agent-untrusted/Desktop/melee-native-miyoo-flip
export FLIP_TOOLCHAIN=$MAIN/build/flip-tools/aarch64--glibc--stable-2023.08-1
export RUSTUP_HOME=$MAIN/build/flip-tools/rustup CARGO_HOME=$MAIN/build/flip-tools/cargo
# Dawn: extract build/flip-tools/dawn-source.tar.gz, git apply dawn-egl-native-window.patch,
# then prepare_flip.py's cmake invocation into build/flip-dawn, installed to:
export FLIP_DAWN_PREFIX=$PWD/build/flip-tools/dawn-install
# Aurora: clone encounter/aurora at d0c931d into build/native-deps/aurora (bootstrap.py),
# git apply native/platform/flip/aurora-flip.patch
native/platform/flip/build.sh
cmake --build build/native-flip --target melee_flip_gpu_probe --parallel 24
python3 native/tools/package_flip.py --output dist/flip/v151-aurora-prs \
    --mali-g29 $MAIN/build/flip-tools/mali-g29p1-candidate/libmali.so.1
```

Verified on 2026-09-14: Dawn (`webgpu_dawn`, AArch64 static) built and installed;
`melee_native` and `melee_flip_gpu_probe` built (AArch64 ELF); the package
`dist/flip/v151-aurora-prs` was produced (`melee_native` sha256
`096e8ca18710d7830a9a01aaf117a0442226738311997c3f604d1875ba043556`). The host
build of the melee CMake tree ran `native_pad`, `native_pad_swap`,
`native_card`, `native_hsd_card` and `native_flip_launcher`: 5/5 passed. The
unstripped binary is kept as `build/flip-tools/melee_native-v151-aurora-prs-symbols`.

## Expected performance

From `FLIP_PERFORMANCE_WINS.md`, "Aurora-only estimate":

> - Fully captured: the GX translation side. The FIFO worker went 28.6 -> ~9 ms
>   through loaders, resident lists, texture identities, memo and instanced
>   points, all in the PR set, and async frames removes the 22 ms/frame join.
> - Partly captured: draws 360 -> ~271 and passes 7 -> 4 (arrays, atlas, fusion,
>   ImGui skip) shrink whatever the render path costs per draw and per pass.
> - Not captured: the render worker still runs Dawn's GL backend. The last
>   measurement of that path on this device (v5x, ~450 draws, 7 passes) was a
>   49.6 ms frame, render-worker bound, before direct GLES took it to 38.6 and the
>   Dawn-side fixes to ~15 ms.
> - Estimate: game ~5 ms and FIFO ~9 ms no longer matter; the frame is the Dawn GL
>   render worker at ~270 draws and 4 passes, roughly 28-35 ms, i.e. ~30 FPS on
>   Onett versus 17 ms / 58 FPS with everything.

So expect roughly **28-35 ms per frame (~30 FPS) on Onett** against the 17 ms
(median 16.7 ms, 52-59 FPS) of the full v143 stack. Two things on this branch
may push the number either way and should be read off the logs, not assumed:
the synchronous DRM page-flip wait now sits on the render worker (compare with
`MELEE_FLIP_ASYNC_PRESENT=1`), and Dawn's Release toggles `skip_validation` /
`disable_robustness` are on, as upstream.

## How to measure when the Flip is back

1. Deploy `dist/flip/v151-aurora-prs`: `python3 native/tools/flip_deploy.py build dist/flip/v151-aurora-prs`
   over LAN ADB, or, through the cloudflared tunnel, one file at a time with
   `native/tools/flip_http_deploy.sh <adb> <serial> dist/flip/v151-aurora-prs/melee_native /mnt/SDCARD/Ports/melee-native/melee_native`
   (and the same for `launch.sh`; `lib/` and `licenses/` are unchanged from v143 apart from README).
   Keep a copy of the v143 `melee_native` and `launch.sh` on the card to switch back.
2. Run the trials on Onett (stage 9) and Battlefield (stage 31), e.g.
   `python3 native/tools/flip_backend_trial.py onett-prs --batch 1 --soak 20` and the
   Battlefield equivalent. The trial script's `--batch`, `--barriers`, vertex
   and table exports are inert on this branch (the binary does not read them);
   pass renderer options with `--env MELEE_FLIP_TEXTURE_ATLAS=0` and friends.
3. Read `[flip-config]` once (confirms the options), then the steady 5-second
   windows: `[perf] presented_fps=... game_render_fps=...`, `[pacing] median_ms
   p95_ms p99_ms max_ms`, `[perf-breakdown] game_ms end_ms sleep_ms begin_ms`
   (with `MELEE_FLIP_PROFILE=1`), and `[flip-present] frame_ms lock_ms drm_ms`
   per frame (this replaces v143's `[flip-thread-present]`; there is no
   presentation thread here, `frame_ms` is the interval between presents on
   the render worker). Compare against the v143 windows in
   `FLIP_HANDOFF.md` / `FLIP_PERFORMANCE_WINS.md`: frozen Onett median 16.7 ms,
   p95 16.8-20.3 ms, 52-59 presented FPS; moving Battlefield 17.1 ms mean.
4. Useful A/Bs: `MELEE_FLIP_ASYNC_FIFO=0` (translation back on the critical
   path), `MELEE_FLIP_RESIDENT_DL=0`, `MELEE_FLIP_TEXTURE_ATLAS=0` (bit-exact
   capture; compare the frozen Onett EFB capture with `MELEE_RENDER_CHECK=1
   MELEE_CAPTURE_FRAME=...` against the `da46d4a79a8f...` reference),
   `MELEE_FLIP_FUSE_PASSES=0`, `MELEE_FLIP_ASYNC_PRESENT=1`.
5. If the renderer fails at start-up, `game.log` shows the Dawn error; the
   two device-specific assumptions that could not be verified without the
   Flip are the fragment-only static bind-group layout under
   `maxStorageBuffersInVertexStage = 0`, and that Dawn's GL backend obtains
   `eglSwapBuffers` through the supplied `getProc` (the scan-out hook depends on
   it; if frames render but the panel never updates, that is where to look).
   `melee_flip_gpu_probe --present` exercises the same path without the game.

## Not verified without the device

- Rendering correctness and any frame time of this Aurora on the Flip; the
  numbers above are the documented estimate.
- The fragment-only storage bind-group layout with the Mali driver, the
  Immediate swapchain on the GBM surface without the presenter, and the
  `eglSwapBuffers` hook.
- Memory: five staging buffers were cut to three and the pools bounded as before;
  the resident geometry budget (64 MiB) is the same as v143's.
- The g13 driver fallback (no barriers on this branch).

## Shader cache

This build must not share a pipeline cache with the Flip-patched builds. Their
cached `PipelineConfig`s decode to storage-buffer vertex shaders, and on the Mali
driver (zero vertex-stage storage blocks) Aurora's cache warm-up then fails with
"Program link failed: The number of vertex shader storage blocks (1) is greater
than the maximum number allowed (0)" and aborts at startup. This was observed on
the device on 2026-09-14 with the trial cache `cache-g29-dense`. The launcher
therefore uses `data/cache/<driver>-aurora-prs`; `flip_backend_trial.py` hardcodes
`data/diagnostics/cache-g29-dense`, so move that directory aside before trials
of this build (and back before trials of the `miyoo-flip` build).

## Installing beside the fast build

The test build lives in `/mnt/SDCARD/Ports/melee-native-dev` and appears in the
Ports list as **Melee Native Dev** (`native/platform/flip/Melee Native Dev.sh`
pushed to `/mnt/SDCARD/Roms/PORTS/`). It reads the main install's disc image
through `MELEE_FLIP_DISC` and keeps its own `data/` (config, saves, shader cache,
logs). Deploy with:

```sh
native/tools/flip_http_deploy.sh ADB SERIAL dist/flip/vNNN/melee_native /mnt/SDCARD/Ports/melee-native-dev/melee_native
python3 native/tools/flip_deploy.py --adb ADB --device SERIAL --root /mnt/SDCARD/Ports/melee-native-dev build dist/flip/vNNN
adb push 'native/platform/flip/Melee Native Dev.sh' /mnt/SDCARD/Roms/PORTS/
```
