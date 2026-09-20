# PortMaster: Melee port, and the existing Dusklight port (research, 2026-09-15)

## What PortMaster expects
- A zip per port: `port.json` (version 4: title, porter, desc, inst, genres, `arch: ["aarch64"]`,
  `min_glibc`), `README.md` (thank-you to the upstream project, controls, build notes),
  `screenshot.jpg` (4:3, gameplay), `gameinfo.xml`, a `Port Name.sh` launcher and a lowercase
  `portname/` data dir with `libs.aarch64/` and `licenses/`. Launcher conventions: source
  `control.txt`, `get_controls`, `$GPTOKEYB` for pad-to-key mapping, `pm_platform_helper`,
  `pm_finish`, keep everything under `$GAMEDIR` (never touch OS libraries).
- Build environment: porters build on old userlands (Ubuntu 20.04/22.04 arm64 rootfs, or the
  `ghcr.io/monkeyx-net/portmaster-build-templates/portmaster-builder` images) so binaries run on the
  oldest CFW glibc; the Dusklight porters' Aurora fork has a commit "Support Bullseye's C++20 library
  subset" (Debian 11, glibc 2.31, GCC 10). Our Flip toolchain targets glibc 2.37 and bundles libstdc++;
  a PortMaster build must be rebuilt against a Bullseye/Focal sysroot.
- Display and input come from the CFW: SDL2 (KMSDRM, fbdev, or Wayland on ROCKNIX), the CFW's own
  Mali/Adreno/Panfrost driver, `gptokeyb`. Ports do not open DRM themselves.
- Submission: PR to `PortsMaster/PortMaster-New` with documented testing on AmberELEC, ArkOS, ROCKNIX,
  muOS at 640x480 and higher. Nintendo-derived reimplementations (Ship of Harkinian, 2Ship, sm64coopdx,
  zelda3, Dusklight) all live in the **Multiverse** repo `PortsMaster-MV/PortMaster-MV-New`, which the
  project does not curate; a Melee port would go there too.
- Miyoo Flip: PortMaster runs on the stock OS through `chrisj951/MiyooFlipPortMaster` (also in
  SpruceOS and Surwish). It ships the muOS prebuilt libs, swaps the Mali driver when launching a port,
  and installs ports under the stock `Ports` listing that `melee-native` already uses.

## The existing Dusklight port (Multiverse, merged 2026-07-29, porters bmdhacks and jckhng)
`ports/dusklight/`: `Dusklight.sh`, a 37 MB `dusklight.aarch64`, `libs.aarch64/libSDL3.so.0`
(3.6 MB), `runtime/TwilitRealm/Dusklight/config.json.default`, `res/`. Tested on ArkOS, ROCKNIX, muOS,
dArkOS at 480x320 to 1280x720; runs on RK3326 (R36S), H700 and up.

How it renders:
- `--backend opengles`: **bmdhacks' hand-rolled GLES 3.0 device** inside Aurora (`lib/gl/*`,
  `bmdhacks/aurora` main, commit "Dusklight GLES backend: carry the fork onto encounter/main f8573d3",
  117 files against upstream). It implements Aurora's internal `webgpu` interface directly, so **Dawn is
  not used at all**: passes, programs, program-binary and pipeline caches, and "native vertex fetch
  (CPU-expanded hardware attributes + content-hash geometry cache)". Upstream's FIFO/command-processor
  refactor, texture replacement streaming and external render targets are deliberately not carried.
- **SDL3-over-SDL2 shim** (`bmdhacks/SDL` branch `sdl2-backend`, shipped as `libSDL3.so.0`): the
  game links SDL3, the shim implements SDL3's video/audio/GPU on top of the CFW's SDL2, so the port
  gets the device's display path (KMSDRM/fbdev/Wayland) without knowing about it. Aurora's GLES device
  presents the EFB through EGLImages the shim provides (`lib/webgpu/sdl2shim_present.cpp`). The launcher
  pins `SDL_VIDEODRIVER=sdl2` (and the inner SDL2 to Wayland on ROCKNIX).
- Handheld defaults: `internalResolutionScale 0.5`, bloom/DoF off, frame interpolation off,
  `decoupleSimFromRender`, performance governors pinned for the session, shader caches wiped when the
  binary changes.
- jckhng first tried the minimal route (`jckhng/aurora` branch `portmaster-upstream-1.4.1-minimal`,
  34 files: keep Dawn's GL backend, add an SDL2-shim presenter and "PortMaster GX submission path"
  tweaks, June-July 2026) before the shipped port moved to bmdhacks' full GL device. That mirrors our
  own measurement: the PR set through Dawn GL is 19 FPS on the Flip, the escape hatch 58.

Independent confirmation of our vertex-path argument: their `RENDERING_NOTES.md` root-causes the
"porcupine" (exploded skinned vertices) on Asahi/Mesa and Adreno/turnip to a driver miscompile of the
lone storage byte-load that feeds the matrix-palette index, shows that no WGSL-level fix survives CSE,
and concludes that **native vertex attributes are the only robust fix and are mandatory on Mali**
(`GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS = 0`). Upstream Aurora's `load_word` guard comment ("discourage
some Adreno drivers... vertex explosions in Dusklight") is aimed at the same bug class.

## How their stack compares with ours
| | Dusklight Multiverse port | melee-native Flip / `zalo/aurora-arm` |
| --- | --- | --- |
| Vertex path | CPU-expanded attributes + geometry cache (fork-only) | `cpuVertexDecode` + resident display lists + instanced points, opt-in, tested, PR-shaped |
| Renderer | full GLES 3.0 device replacing Dawn (117 files, not upstreamable as-is) | Dawn kept; nine upstream-shaped PRs + GLES direct submission through a Dawn interop extension, byte-identical toggles |
| Presentation | SDL3-over-SDL2 shim (works on every PortMaster CFW) | own DRM/GBM/EGL display code (Flip stock OS only) |
| Draw batching / uniform table | unknown (not described) | measured: draws halved, bit-exact |
| Measured | no public numbers found; runs on RK3326-class devices | frozen Onett 58-59 FPS on RK3566 |

The shim is the piece we lack for PortMaster and it is orthogonal to the renderer: it solves "how does
an SDL3 Aurora app get a window on a CFW", not per-draw cost. Whether their GL device is as fast as our
fast path is not knowable from the repos; the test is to run their `dusklight.aarch64` and a Dusklight
built on our Aurora on the same device and scene (needs our Dusklight Flip build first; see
`~/Desktop/aurora-bench/dusklight-flip.md`).

## Melee PortMaster port: plan
1. **Presentation:** adopt the SDL3-over-SDL2 shim instead of `native/platform/flip/display.cpp`.
   Aurora's `BackendBinding.cpp` needs a case for the shim's EGL surface (the Dawn
   `SurfaceSourceEGLNativeWindow` from our Dawn PR is exactly the surface type), and the GLES fast
   path's present worker needs to hand frames to the shim (or fall back to Dawn's swapchain present).
   Alternative with no shim: static SDL3 with KMSDRM, but CFWs differ (fbdev on RK3326, Wayland on
   ROCKNIX), which is why the shim exists.
2. **Toolchain:** rebuild against a Bullseye/Focal aarch64 sysroot (PortMaster docker image), drop the
   bundled Mali driver (the CFW provides one; the stock-Flip PortMaster layer swaps it), keep bundled
   libstdc++ only if the target glibc allows it, set `min_glibc`.
3. **Launcher:** `Melee.sh` following `Dusklight.sh` (governors, `gptokeyb`, log tee, cache wipe on
   binary change), disc image from `melee/assets/*.iso|*.ciso|*.rvz`, saves under `$GAMEDIR/runtime`.
   Our `MELEE_FLIP_*` env defaults become the port's config; one pipeline cache per renderer variant.
4. **Controls:** replace the Flip-specific mapping (D-pad/stick and A/B swap) with `gptokeyb`/SDL
   controller config from `get_controls`; keep the swap as an option.
5. **Devices:** RK3566 (Flip, RG353) is the proven target; RK3326 has half the CPU and a Mali-G31,
   where the fast path matters even more and 60 FPS is unlikely; RK3588 and Snapdragon devices have
   Vulkan, where the PR set alone applies.
6. **Submission:** Multiverse PR with the four-CFW testing checklist; screenshot of a match.

## Dusklight on PortMaster: contribute rather than fork
The port exists and is maintained (crash fix merged 2026-09-13). The useful contribution is on the
renderer: (a) offer the upstream-shaped PR set (vertex loaders, resident display lists, texture
identity, memo, async frames, texture arrays, pass fusion) to bmdhacks/jckhng as a replacement for the
fork's private vertex/geometry cache, since both sides now agree native vertex fetch is mandatory;
(b) measure their GL device against our Dawn-plus-direct-submission path on one device; (c) if ours is
faster or equal, propose the shim + our Aurora as the port's next base, which would also be the
vehicle for the Melee port. First contact: the Multiverse PR threads (#145, #151) and the RENDERING_NOTES
authors.

## Status (branch `portmaster`, 2026-09-15)

### Full-speed gameplay on slow renderers: catch-up frame pacing (2026-09-19)
- Before this, one rendered frame was one game frame: `VIWaitForRetrace` delivered a single retrace
  per call and re-based its clock when late, so the RG351P's 20 FPS match ran at a third of real
  time and an online session ran at the slower device's rendered rate. The console never behaved
  that way: its retrace is a hardware interrupt, every retrace polls the pads into HSD's five-entry
  queue, and Melee's scene loop (`gmscene.c`) runs one update per queued poll before it draws.
- `native/vi_pacing.h` + `vi_runtime.cpp`: when the game thread arrives late by whole 16.7 ms
  periods, the wait delivers one retrace per missed period (each runs the HSD pre/post callbacks,
  which only rotate XFBs when a frame is pending, and `lb_0195.c`'s pad poll), capped at 3 per wait
  (`MELEE_VI_CATCHUP=N`, 0/1 = off) so a shader-compile or disc stall never becomes a burst; debt
  beyond the cap is dropped (the game slows, as before). `native/tests/vi_pacing_test.cpp` covers the
  arithmetic (`ctest -R native_vi_pacing`).
- The `[perf]` line gained `logic_fps=` (the game's update rate; `presented_fps` is the picture),
  `catchup_retraces=` (extra polls delivered) and `dropped_periods=` (16.7 ms slots the game could
  not make up: nonzero means it ran slower than real time in that window). `[perf-breakdown]`'s
  `updates_per_frame` no longer resets the logic-frame counter.
- **Single-player only; off in deterministic/online mode.** Frame skipping means a device updates
  more times than it renders, and Melee's HUD and item code read joint state computed during the
  render: the damage-number shake in `ifstatus.c` draws gameplay RNG a render-count-dependent number
  of times, so two devices at different frame rates would desync (proven: an online match with a
  bat item desynced at frame ~2300 whether catch-up was on or off, the moment the clamp was gone;
  found with `MELEE_TRACE_RAND` + an ordered per-frame draw diff, first divergence in
  `ifStatus_802F4EDC`). So `vi_runtime.cpp` forces the cap to 1 when `MeleeNativeDeterministicIO()`
  and `gmscene.c` keeps the one-update-per-iteration clamp for deterministic mode. Online play stays
  exactly as it was (one render per logic frame on both peers, the match at the slower device's
  pace); single-player gets the frame-skip win. Making online itself run the faster device ahead
  would require removing every simulation read of render-computed state (HUD, items, effects) and is
  out of scope here.
- Release build with catch-up: `dist/portmaster/melee.zip` binary md5 `654bd5aa`; 17 package tests
  pass. (The intermediate `c68555af`/`8bb1ed38` build with a particle-sort experiment was reverted
  once the control showed the desync was pre-existing, not caused by catch-up.)
- Harness: `pmrun-matrix.sh` now kills Melee.sh's leftover PortMaster dialog helpers (`pugwash
  fifo_control`) at case end; three of them (~50 MB each) had pushed a Flip run into a four-minute
  thrash with SSH unreachable (dt15).
- Measured (warm shader cache; the first run after any new binary compiles every shader, since the
  cache directory is keyed by the executable's size and mtime, and that cold run reads 10 FPS on
  the Flip with the renderer waiting up to a minute for a frame slot: not a regression):
  - Flip ROCKNIX libmali g29p1, scripted VS match (fs2): presented 55-59 FPS, `logic_fps` 59.3-60.1,
    2-16 catch-up retraces per 5 s window, 0-11 dropped periods. Unchanged picture, no slow-down.
  - RG351P AmberELEC (fs1-fs5): `logic_fps` 28-44 (was 20-24 with the picture at the same rate),
    presented 17-32 FPS, 1.4-2.1 updates per drawn frame; steady-state frames are 17-20 ms so the
    game does keep real time between stalls, but `dropped_periods` runs 90-190 per 5 s: AmberELEC
    stalls 150-450 ms several times a window (p95 120-380 ms), and those account for the rest.
    Not shader compiles (`pipeline_wait_ms=0`), not the present call (`[flip-thread-present]`
    present_ms > 30 ms on 4 of 4,945 frames), not the CPU/DDR/GPU governors (all three set to
    performance in fs3/fs5: median 17-19 ms and +5 FPS, stalls unchanged; AmberELEC runs
    `dmc_ondemand` at 528 MHz DDR by default). The game thread spends those stalls asleep in the
    frame-slot wait, so the FIFO/render side is what blocks; Batocera 42 and dArkOS on the same
    RG351P show p95 22-42 ms with no such stalls, so it is AmberELEC-specific (kernel 4.4.189,
    libmali r13p0, KMSDRM+RGA rotation SDL2). Open lead for a later session; `perf_event_open` is
    not implemented on that kernel, so use gdb batch backtraces or Aurora-side timers.

### SDL: shared SDL3 over the CFW's SDL2 (2026-09-19, after tester feedback)
- Testers and PortMaster maintainers objected to the built-in SDL3 ("dynamically link it so it uses
  the system's SDL build", "what about fbdev CFWs like H700 Knulli and muOS?"). They are right: SDL3
  has no fbdev driver, so the RG35XX/RG40XX/TrimUI class could never run the static build, and every
  CFW's SDL2 carries device patches (RG351P/RG552 RGA rotation, connector quirks, audio server).
  The port now links SDL3 as a shared library and ships bmdhacks' SDL3-over-SDL2 shim
  (`bmdhacks/SDL` branch `sdl2-backend`, commit 6057d79, SDL 3.5.0; the library Dusklight ships) as
  `melee/libs.aarch64/libSDL3.so.0`: `native/tools/build_sdl3_shim.sh` builds it with the glibc 2.30
  toolchain (`SDL_SDL2_BACKEND=ON`, native X11/Wayland/KMSDRM/audio drivers off, `SDL_GPU=OFF` so no
  SPIRV-Cross). `build.sh` takes `MELEE_SDL=shim|static` (`build_portmaster.sh` defaults to shim:
  `AURORA_SDL3_PROVIDER=system`, `AURORA_SDL3_LINKAGE=shared`, `SDL3_DIR` from `FLIP_SDL3_ROOT`, which
  the toolchain files add to `CMAKE_FIND_ROOT_PATH`). Runtime pieces: Dawn's swapchain wraps no native
  window on the shim (`MeleeFlipNativeWindow()` null) and the Dawn patch now backs a null
  `SurfaceSourceEGLNativeWindow` with a pbuffer, or with no EGLSurface at all when the display has no
  pbuffer configs (Mesa's Wayland and GBM platforms; needs EGL_KHR_surfaceless_context and the GL
  interop presenter, which takes the texture) (`SwapChainEGL::CreateEGLSurface`,
  `PhysicalDevice::GetSurfaceCapabilities` accepts window or pbuffer configs for that surface;
  `dawn-gl-interop.patch` regenerated from the pristine 1155e0ed tree, 14 files); the present
  worker keeps blitting into SDL's window surface and swapping through SDL3 → SDL2; Aurora reuses the
  port's window instead of creating a hidden second one (`MeleeFlipSdlWindow()`, Aurora 0fab9c6).
  `Melee.sh` adds `libs.aarch64` to `LD_LIBRARY_PATH`, hands the CFW's `SDL_VIDEODRIVER` /
  `SDL_AUDIODRIVER` (SDL2 driver names, e.g. ROCKNIX wayland/pulseaudio) to the inner SDL2 through
  `SDL3SHIM_SDL2_VIDEODRIVER` / `_AUDIODRIVER`, and sets SDL3's own drivers to `sdl2`.
  `check_sdl_backends.sh --shim` verifies the packaged pair; `package_portmaster.py --sdl3` ships the
  library and its notice (`LICENSE.SDL3-sdl2-backend.txt`); CI caches the shim with the SDK.
- Driver-name handoff: the shim copies `SDL3SHIM_SDL2_VIDEODRIVER` over `SDL_VIDEODRIVER` only when
  it is set and non-empty, and the CFW's SDL2 inside the shim reads `SDL_VIDEODRIVER` too. On CFWs
  that name no driver (AmberELEC, Knulli; EmulationStation exports no `SDL_*` there) the inner SDL2
  therefore saw our `sdl2` and failed with "sdl2 not available" (the video path survived through
  `initSdlVideo()`'s fallback, audio did not: the shim ignores the inner `SDL2_Init(AUDIO)` failure and
  every `SDL2_OpenAudioDevice` then fails). `initSdlVideo()` and `initAudioSubsystem()` now move an
  `sdl2` driver choice from the environment into an SDL3 hint (`SDL_HINT_OVERRIDE`) and unset the
  variables before `SDL_InitSubSystem`, so the inner SDL2 picks its own default (KMSDRM, fbdev, ...)
  and the shim's explicit `SDL3SHIM_SDL2_*` override still applies where the CFW names a driver.

- No display-stack libraries in the binary's NEEDED list (2026-09-19, after the user's "we cannot fall
  back to the direct display"): the direct DRM/GBM path (`MELEE_FLIP_DISPLAY=drm`, Flip stock firmware
  only; never selected by `Melee.sh`, and a failed SDL init exits with "Cannot initialize SDL video"
  rather than falling back to it) used to link libdrm.so.2, so the port would not even load on a CFW
  whose SDL2 runs on fbdev without libdrm. `display.cpp` now dlopens libdrm like it already did libgbm
  (`drm_runtime`, all-or-nothing; the drm path fails with a clear message when the library is missing),
  `CMakeLists.txt` drops `drm` from the link, and `check_sdl_backends.sh --shim` plus
  `test_portmaster_package.py` fail when NEEDED contains libdrm/libgbm/libwayland/libSDL2/libstdc++.
  NEEDED is now libEGL/libGLESv2 (the CFW's GL driver), libSDL3.so.0 and libc-level libraries only.

### Crash reporting and the pipeline-cache crash loop (2026-09-19, Pi 5 report)
- A Batocera 43.1 Raspberry Pi 5 (V3D, Mesa 25.3.6 GLES 3.1) reached the menu, segfaulted when the
  match started (a shader compile inside the driver, presumably) and then segfaulted at every launch
  right after "Using surface format", i.e. while the pipeline worker recompiled the cached pipelines.
  `runtime_main.cpp` now installs a crash handler (SIGSEGV/SIGBUS/SIGILL/SIGFPE/SIGABRT) that writes
  `[crash] signal`, the faulting address, the executable's map line and a raw backtrace to log.txt
  (symbolize against the `melee-portmaster-symbols` artifact of the same release with the map base),
  and keeps a marker in the cache root: "init" until the first simulated frame, "running" after,
  removed on a clean exit or SIGTERM/SIGINT/SIGHUP. A launch that finds "init" left behind sets that
  build's `pipeline-*` directory aside (`.crashed`) and starts empty; a run that had been playing keeps
  its cache whatever killed it (the matrix's kill -9 included; `pmrun-matrix.sh` now sends SIGTERM
  first). `Melee.sh` shows "Melee crashed (signal N)" for a signal exit. Verified on the Flip: two
  consecutive harness runs keep the cache and leave no marker.

### Releases and CI caching (2026-09-19)
- `.github/workflows/portmaster.yml` runs on `portmaster` and `release`. A push to `release` adds the
  `release` job: it downloads the `package` artifacts and runs `gh release create` with tag
  `portmaster-<yyyymmdd>-<sha7>`, `melee.zip`, `melee-portmaster-symbols.tar.gz` and notes rendered by
  `native/tools/release_notes.py` from `native/platform/portmaster/RELEASE_NOTES.md` (install through
  `ports/PortMaster/autoinstall/`, disc image, controls, what to attach to a report, SHA-256).
  Release = `git push zalo <commit>:release`; testers get the notes and zip from the Releases page.
- Caches: `portmaster-sdk-*` (SDK, glibc 2.30 hybrid, wayland-arm64, sdl3-shim-install; key = prepare
  scripts), `portmaster-dawn-<DAWN_CACHE_VERSION>-*` (dawn-install-a35, rustup, cargo; key = Dawn patch
  + toolchain files; a prefix-only restore now deletes dawn-install-a35 so Dawn is rebuilt with the new
  patch instead of silently reused), `portmaster-ccache-<sha>` (ccache in front of the cc/cxx wrappers,
  `CCACHE_COMPILERCHECK=%compiler% --version`, 2 GB, restored from the newest run). Timings: Dawn rebuild ~20 min; cached
  run without ccache 6 min ("Build and package" 4.5 min); with a warm ccache 2 min end to end ("Build
  and package" 54 s, run 35456671875). The ccache stats land in the job summary.

### Testing round 2 (2026-09-19, shim build on hardware)
- Matrix `match` case (`build/matrix/matrix.py`, results under `build/matrix/results/<device>/sh1-*`
  for the first shim build 9eba154d and `sh2-*` for the fixed build 9aef2cff):
  - Flip ROCKNIX 20260902 (libmali g29p1, run with EmulationStation's `SDL_VIDEODRIVER=wayland`
    `SDL_AUDIODRIVER=pulseaudio` `XDG_RUNTIME_DIR` `WAYLAND_DISPLAY` via `--env`): PASS 19/19, match at
    53-60 FPS, `SDL display (sdl2)` with the inner SDL2 on Wayland, `[audio] driver=sdl2 device=System
    audio playback device` through PulseAudio (the tester's silent ROCKNIX is fixed for the shim path).
  - Flip Knulli (KMSDRM, libmali g13p0, `--scale 2.5`): PASS 19/19 with both builds, median 9 FPS as with
    the static builds (this blob trips the driver probe: per-draw barriers). The first build went through
    the driver fallback; the fixed one opens the display directly. Audio failed in both SSH-launched runs
    (`SDL2_OpenAudioDevice failed`; the static build's k1 run failed the same way with ALSA "Host is
    down"): Knulli's EmulationStation gives ports `XDG_RUNTIME_DIR=/var/run` and `SDL_NOMOUSE=1` and no
    `SDL_*DRIVER`; with `--env XDG_RUNTIME_DIR=/var/run` (`knulli-roundtrip.sh sh3`, banner build f91fcfd1)
    the run PASSed with `[audio] driver=sdl2 device=System audio playback device`, so Knulli audio works
    from the CFW's launcher and the earlier silence was the SSH environment.
- Slow-driver reporting (user request, 2026-09-19): Aurora's driver probe only drew its "GPU driver update
  needed" notice for 20 s starting a few seconds after launch, during the intro movie, so a Knulli run
  looked like an unexplained 9 FPS port afterwards. Aurora 8ee1078 keeps a one-line banner at the bottom
  edge after the notice (`gles_direct::driver_banner()`, worded "rendering slowly" when the probe measured
  the barriers as expensive) and exports `aurora_gl_driver_notice()`; `vi_runtime.cpp` prints
  `[perf] GPU driver workaround active: ...` once and appends `driver_workaround=per-draw-barrier` to every
  `[perf] presented_fps=` line; `matrix.py` adds a `notes` column ("per-draw barrier (slow driver
  workaround)") to the summary and its progress lines so the FPS column is never read as comparable.
  - RG351P AmberELEC prerelease-20250515 (SDL2 2.32.4 KMSDRM with the RGA rotation patch, panel reported
    as 480x320 landscape, `rotate 0`): PASS 19/19, median 19 FPS, same as the static builds; first shim
    build via the fallback without audio, fixed build opens the display and audio directly.
  - QEMU stand-in (`/data/agent-untrusted/qemu-melee/run.sh INNER_VIDEO=none`): fixed build without any
    `SDL3SHIM_SDL2_*` set brings up display and audio without the fallback line.

### Testing round 1 (2026-09-19)
- Two tester reports on an RG552 (RK3399, 1920x1152). AmberELEC: "No available video device";
  ROCKNIX (Panfrost): runs at 57-60 FPS after a slow first match, pillarboxed 4:3, rumble works,
  **no sound**. Root causes: the shipped build had been configured before its sysroot got the
  libdrm/gbm `.pc` files, so SDL silently dropped the KMSDRM driver (only Wayland was compiled in),
  and ROCKNIX exports `SDL_AUDIODRIVER=pulseaudio` to ports while SDL had no PulseAudio backend.
  Fixes: `prepare_flip.py` fetches the PulseAudio/PipeWire client headers, libraries and `.pc`
  data (Debian bookworm arm64) into the sysroot for every build mode; `build.sh` writes the sysroot
  `.pc` files before configuring, turns `SDL_PULSEAUDIO`/`SDL_PIPEWIRE` on (dlopen) and runs
  `native/platform/flip/check_sdl_backends.sh`, which fails unless SDL's generated config and the
  binary carry KMSDRM + Wayland and ALSA + PulseAudio + PipeWire; CI runs the same check on the
  packaged binary and `test_portmaster_package.py` checks the zip. `audio_host.cpp` and
  `display.cpp` honour the CFW's `SDL_AUDIODRIVER`/`SDL_VIDEODRIVER` but fall back to SDL's own
  probe order when that backend is unavailable, and a display failure now logs the built-in
  drivers, `/dev/dri` and SDL's per-driver debug reasons. The hybrid glibc 2.30 toolchain carries a
  layout stamp so a cached copy without the new headers is rebuilt. The tester's "you statically
  linked SDL" point is answered in `testing_thread.txt`: SDL 3 is compiled in (Aurora needs it),
  loads the CFW's libdrm/libgbm/libwayland/libasound/libpulse/libpipewire by soname, and does not
  shadow the CFW's SDL 2; rotation is handled by the port.

### What was built
- **Upstream decompilation merged**: `doldecomp/master` `3880e77a8` (81 commits since the
  previous base `05a1394fa`) merged into the native port. 51 conflicting files were resolved
  as "upstream text plus the port's `MELEE_NATIVE` intent"; where upstream adopted the
  equivalent fix (tagged-union card library, `void*` stage script pointers, `mncharsel`
  globals, `gmscene.c` split, `intptr_t` in mplib) the port hunk was dropped. Native-side
  fallout: SDK headers moved from `extern/` to `libs/` (`prepare_sdk.py`), `OSPanic`/
  `__assert`/`HSD_Panic` prototypes are `const char*` + noreturn, `CKIND_*` -> `CKind_*`,
  `seed_ptr` -> `HSD_RandSeedPtr`, event data field names in `asset_schema.c`.
- **Aurora pinned to the fork**: `native/tools/bootstrap.py` clones
  `https://github.com/zalo/aurora-arm.git` branch `gles-direct-submission` at
  `f6275d6995cd4966cc993cc45a1530bafc2f297e` (upstream Aurora + the nine perf PRs + Flip
  platform hunks + GLES fast path; 0fab9c6 on 2026-09-19 makes Aurora reuse the application's SDL
  window on MELEE_MIYOO_FLIP for the SDL2 shim; previous pin 2d943c98: upstream + the nine perf PRs + Flip
  platform hunks + GLES fast path + the frame-stream overflow fix; since 2026-09-18 also the
  configurable uniform window, the GL driver probe with its per-draw barrier fallback, the
  staging buffers sized without the stream regions when mapped GL streams are active, which is
  what makes Mesa/Panfrost fit in 1 GiB, and the merge of `encounter/aurora` main with the
  aurora/card.h API and CARD rewrite). `aurora-flip.patch` is
  gone (it reverse-applied cleanly on the fork). `MELEE_AURORA_EXPECTED_REV` in
  `native/CMakeLists.txt` is the matching cache variable (`-DMELEE_AURORA_DIR=<checkout>
  -DMELEE_AURORA_EXPECTED_REV=<sha>` for review builds). `AURORA_VERTEX_BUFFER_MIB` is not
  passed in the Flip/PortMaster build: `lib/gfx/resources.hpp` on the branch still sets the
  12 MiB vertex stream under `MELEE_MIYOO_FLIP`, so the variable is redundant there.
- **Dawn**: `encounter/dawn` `1155e0e` plus `native/platform/flip/dawn-gl-interop.patch`
  (the `gl-native-interop` branch of `zalo/dawn-aurora-arm`, tip `e1a7185`, as one diff:
  EGL native-window surface source, swapchain GL storage reuse, native GL interop
  extension). That is what the `dawn-install-fast` prefix the cross build links was built
  from. The former `dawn-egl-native-window.patch` was byte-identical and was renamed.
- **PortMaster packaging** (`native/platform/portmaster/`): `Melee.sh` launcher following
  the Dusklight port (control.txt/mod_CFW, `get_controls`, log tee, disc discovery in
  `melee/assets` for `.iso/.gcm/.ciso/.rvz`, XDG dirs under `melee/runtime`, one pipeline
  cache per binary, generic governor/core-online handling with restore on every exit path,
  `$GPTOKEYB` exit hotkey, `pm_platform_helper`, `pm_finish`, bundled Mali g29p1 only on
  `rk3566` device trees with `MELEE_PM_DRIVER=system|bundled` override,
  `MELEE_PM_SWAP_CONTROLS` mapped onto the existing `MELEE_FLIP_SWAP_CONTROLS` switch),
  `port.json` (v4), `README.md`, `gameinfo.xml`, `screenshot.png` (640x480 Onett scanout
  from the fast-path trial), `build_portmaster.sh` (bootstrap -> prepare -> cross build ->
  package), `native/tools/package_portmaster.py` (reuses `package_flip.py`),
  `native/tests/test_portmaster_package.py` (port.json, gameinfo, launcher text and
  behaviour against a fake control folder/sysfs/device tree, packager layout, built zip).
- **Display generality** (`native/platform/flip/display.cpp`): connector selection honours
  `MELEE_DRM_CONNECTOR=<index>` (plus the existing `MELEE_DRM_DEVICE`), falls back from the
  active encoder's CRTC to any CRTC an encoder of the connector can drive, uses the
  connector's preferred mode (then the current CRTC mode, then the first mode) and renders
  at the mode size: 640x480 on the Flip, the mode size elsewhere. `MeleeFlipDisplaySize()`
  feeds `AuroraConfig.windowWidth/Height`. No rotation.
- **Developer menu, Port Settings and debug overlays (2026-09-18, first item of
  `native/FEATURE_FEASIBILITY.md`)**: Y on the title screen opens the game's own developer
  menu on every native build (`gmtitle.c`, `gmtitlemode.c`, `gmopeningmode.c`; retail ignores
  Y there). Its first row, "Port Settings" (`src/melee/if/soundtest.c`, `MELEE_NATIVE`), edits
  `native/settings.c`'s key = value file `<userPath>/settings.cfg` (`melee/runtime/config/
  melee-native/settings.cfg` on PortMaster; `native/include/melee_settings.h`; unit test
  `native/tests/settings_test.c`): `debug_overlays` (0/1), `debug_level` (0 retail, 1..4
  forced `DbLevel` at the next launch, applied in `gmmain.c` after `gmMain_8015FDA4`), plus an
  "Unlock All Characters+Stages" action (`gm_80164F18` + `gm_8016468C` + card save, the
  development build's new-save behaviour). `MELEE_DEBUG_OVERLAYS` / `MELEE_DEBUG_LEVEL` override
  the file for one launch. Overlays without a debug level: `db_HasOverlays()` in
  `src/melee/db/db.h` replaces `DbLevel >= DbLKind_DebugRom` only at the overlay sites
  (`db_Setup`, `db_RunEveryFrame`, the debug pause/frame-step hook in `gmscene.c`), so the
  gameplay changes tied to the debug level (`ft_0881.c` stale-move decay, `fighter.c` C-stick,
  `gmvs.c` chords and asserts) stay retail; in that mode `gmvs.c` installs the non-VS debug
  pause chord (X + D-pad up, Z steps) because Start still pauses normally. The input-script
  replay gained GameCube D-pad tokens (`DU/DD/DL/DR`) and `SCENE_DEBUG_MENU`; matrix cases
  `debugmenu` and `overlays` (frame capture) cover the feature on the devices.

### What was tested (builds and host tests only)
- Host Linux build (`clang`, RelWithDebInfo, Aurora `04448f6` worktree): `melee_native` and
  every test target link; `ctest -L melee`: 23/23 passed, including `native_hsd_card`
  (end-to-end save/load through upstream's rewritten card library) and `native_pad_swap`.
- AArch64 cross build (`native/platform/flip/build.sh` with the Bootlin SDK and the
  `dawn-install-fast` prefix): `melee_native` and `melee_flip_gpu_probe` link.
- `python3 native/tests/test_portmaster_package.py`: 14/14 (with the built zip);
  `test_flip_launcher.py`, `test_flip_prepare.py`, `test_flip_deploy.py`, `test_disc.py`: OK.
- Package: `dist/portmaster/melee.zip` (19,481,568 bytes,
  sha256 `cf4e1c0b9531b0d2e37f5e26cc9d218118159138e4eacb58554f40205afb5194`) and the
  unzipped `dist/portmaster/melee/` tree; the binary is a stripped ELF64 AArch64 PIE with
  the bundled g29p1 driver. Symbols: `build/flip-tools/melee_native-portmaster-symbols`.

### What is untested
- Every real device. The Flip was offline for this work: the merged game code, the new
  display selection, the launcher under a real PortMaster `control.txt`, the CFW GLES
  driver path (`MELEE_PM_DRIVER=system`), non-640x480 panels, RK3326/H700/RK3588 devices,
  and PortMaster's installer handling of the zip have not been run anywhere.
- Runtime behaviour of the merged decompilation (81 upstream commits) on any platform.
- `build_portmaster.sh` end to end from a clean machine (its pieces were run individually
  with pre-existing SDK/Dawn prefixes).

### Exact commands used
```sh
# worktrees
git -C ~/Desktop/melee-native-miyoo-flip worktree add ~/Desktop/melee-wt/portmaster -b portmaster miyoo-flip-aurora-prs-fast
git -C ~/Desktop/aurora worktree add --detach ~/Desktop/melee-wt/portmaster-aurora 2d943c982f5d7b4582d7a66ccd1961e87e160288
cd ~/Desktop/melee-wt/portmaster
git merge doldecomp/master        # then resolve, see the merge commit message

# host compile check + tests
CC=clang CXX=clang++ cmake -S native -B build/native-linux -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DMELEE_AURORA_DIR=$HOME/Desktop/melee-wt/portmaster-aurora \
  -DMELEE_AURORA_EXPECTED_REV=2d943c982f5d7b4582d7a66ccd1961e87e160288
cmake --build build/native-linux --target all melee_native --parallel 12
ctest --test-dir build/native-linux -L melee --output-on-failure

# cross build (Aurora checkout from bootstrap.py), package
python3 native/tools/bootstrap.py
MAIN=$HOME/Desktop/melee-native-miyoo-flip
FLIP_TOOLCHAIN=$MAIN/build/flip-tools/aarch64--glibc--stable-2023.08-1 \
FLIP_DAWN_PREFIX=$HOME/Desktop/melee-wt/miyoo-flip-aurora-prs/build/flip-tools/dawn-install-fast \
RUSTUP_HOME=$MAIN/build/flip-tools/rustup CARGO_HOME=$MAIN/build/flip-tools/cargo BUILD_JOBS=12 \
PATH=$MAIN/build/flip-tools/cargo/bin:$PATH \
sh native/platform/flip/build.sh -DRust_RUSTUP=$MAIN/build/flip-tools/cargo/bin/rustup -DRust_TOOLCHAIN=stable-x86_64-unknown-linux-gnu
FLIP_TOOLCHAIN=$MAIN/build/flip-tools/aarch64--glibc--stable-2023.08-1 \
python3 native/tools/package_portmaster.py --output dist/portmaster \
  --mali-g29 $MAIN/build/flip-tools/mali-g29p1-candidate/libmali.so.1
python3 native/tests/test_portmaster_package.py
# or, all of the above after bootstrap in one go:
FLIP_TOOLCHAIN=... FLIP_DAWN_PREFIX=... MALI_G29=... sh native/platform/portmaster/build_portmaster.sh
```
