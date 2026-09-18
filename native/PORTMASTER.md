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
  `b1f46212a36cc37d73fb3a4369a84106ca8f7ea5` (upstream Aurora + the nine perf PRs + Flip
  platform hunks + GLES fast path + the frame-stream overflow fix; since 2026-09-18 also the
  configurable uniform window, the GL driver probe with its per-draw barrier fallback, and the
  staging buffers sized without the stream regions when mapped GL streams are active, which is
  what makes Mesa/Panfrost fit in 1 GiB). `aurora-flip.patch` is
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
git -C ~/Desktop/aurora worktree add --detach ~/Desktop/melee-wt/portmaster-aurora b1f46212a36cc37d73fb3a4369a84106ca8f7ea5
cd ~/Desktop/melee-wt/portmaster
git merge doldecomp/master        # then resolve, see the merge commit message

# host compile check + tests
CC=clang CXX=clang++ cmake -S native -B build/native-linux -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DMELEE_AURORA_DIR=$HOME/Desktop/melee-wt/portmaster-aurora \
  -DMELEE_AURORA_EXPECTED_REV=b1f46212a36cc37d73fb3a4369a84106ca8f7ea5
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
