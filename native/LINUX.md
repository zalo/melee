# Native Linux port

Linux and macOS share the `main` branch. Linux-specific launcher and package
files are in `native/platform/linux`; the game and runtime are shared.

This x86-64 Linux port runs recovered Melee game code through Aurora/Dawn Vulkan
and SDL3. Original upstream: [doldecomp/melee](https://github.com/doldecomp/melee).
It requires your own Melee US 1.02 image (GALE01 revision 2). Source builds and
component tests require no image. Both game font atlases load from the selected
image at runtime; no disc, extracted assets, fonts or firmware are packaged.

## Ubuntu 24.04 build

Install development packages (administrator action on a normal installation):

```sh
sudo apt-get install clang libclang-rt-18-dev ninja-build cmake python3 ruby pkg-config patchelf zstd \
  libpng-dev libfreetype-dev libzstd-dev libx11-dev libxext-dev libxcursor-dev \
  libxi-dev libxrandr-dev libxfixes-dev libxss-dev libxtst-dev libwayland-dev \
  libxkbcommon-dev libdecor-0-dev libasound2-dev libpulse-dev libudev-dev \
  libdbus-1-dev libegl-dev libgl-dev
python3 native/tools/bootstrap.py
CC=clang CXX=clang++ cmake -S native -B build/native-linux -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/native-linux --target melee_native all --parallel 3
ctest --test-dir build/native-linux -L melee --output-on-failure
./build/native-linux/melee_native
```

Aurora is pinned to `749d6ee7a22bdfab78c8ece9047bca5d79aa72ca`; bootstrap preserves
an existing checkout and rejects an unexpected revision. Dependencies download
on the first configure, including Aurora's pinned Linux x86-64 Dawn and nod
packages. Clang is required for the recovered source's compiler extensions.
`melee_native` is the shared CMake target. On macOS, `melee_mac` remains a build
target and produces the existing `melee_mac.app` layout and executable name.
The macOS workflow retains Xcode 26.2. Linux changes need separate Mac regression
validation before release.

The validation ThinkPad has no passwordless sudo. Its missing Ubuntu packages
were downloaded with `apt-get download` and extracted into an ignored local
sysroot, without modifying OS packages or drivers. Reproducible local commands
and logs are recorded in `build/linux-task/status.md` on that machine.

## Running

Launch from your active desktop, with its DISPLAY/WAYLAND_DISPLAY and session
bus environment. The asynchronous SDL picker pumps desktop events until its
callback returns. Cancel exits; an invalid image reports an error and permits
another choice. A path can also be passed as the sole command-line argument;
invalid images then fail with a terminal diagnostic and nonzero status.

Vulkan is required. Install the appropriate hardware driver for your existing
GPU; no CPU rendering fallback is enabled. To select a driver explicitly on
Ubuntu, for example:

```sh
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/intel_icd.json ./build/native-linux/melee_native
```

On the validation ThinkPad, default selection chooses the MX150/NVK and the
control match ran at roughly 42 FPS. Explicit Intel UHD620 selection produced
the substantially faster full matrix results. See the [validation report](validation/2026-09-09-linux/REPORT.md).

Check the actual adapter and driver in the launch log. A llvmpipe/software run
is not hardware validation. X11 and Wayland support are compiled into SDL;
Wayland still needs a compatible desktop portal file chooser. Audio uses SDL's
available PulseAudio/PipeWire compatibility service or ALSA device.

Keyboard: WASD move, X attack/confirm, Z special/back, C/V jump, Q/E shield,
R grab, IJKL C-stick, Return start/pause. Focus the game window. Choose **No**
at the initial save prompt; saving is an inherited incomplete feature.

Graphical logs use `$XDG_STATE_HOME/melee-native/game.log` (default
`~/.local/state/melee-native/game.log`). Settings use
`$XDG_CONFIG_HOME/melee-native` (default `~/.config/melee-native`); renderer
cache uses `$XDG_CACHE_HOME/melee-native` (default `~/.cache/melee-native`).
Relative XDG values are ignored per XDG conventions. Command-line logs stay
on the terminal. Each graphical launch asks for an image again.

## Integration tests and limits

The existing Ruby PAD scripts now choose Linux executable/disc defaults and
perform numeric GPU checks without screenshots or videos:

```sh
export MELEE_TEST_DISC=/absolute/path/to/your/melee-us-1.02.ciso
export MELEE_RENDER_CHECK=1 MELEE_AUDIO_CHECK=1
ruby native/tools/run_input_test.rb native/tests/match-controls.input 240
ruby native/tools/run_matrix.rb combined
```

`MELEE_TEST_CLEAN_LIBRARY_ENV=1` removes library overrides from the game process
for relocated bundle testing. `MELEE_TEST_CWD` selects its working directory.
`MELEE_TEST_APP` selects a different executable; `MELEE_TEST_TIMEOUT` sets each
matrix run's wall-clock timeout. Logs go into ignored `build/native-runs` and
`build/native-matrix` directories. The matrix changes fighters and items between
stage runs to cover 29 VS stages, 26 character entries and 35 common item kinds.
It checks actual stage/fighter selection, item spawns, finite camera matrices,
and post-countdown playfield color/edge variation. Nonblack alone can accept a
solid blue background and is insufficient. FPS and presentation interval
median/p95/p99/max are logged every five seconds; initial loading/shader stalls
are included and should be distinguished from steady gameplay.

This is short-match coverage, not every move, effect, costume, mode or matchup.
Saving, adventure/target stages, exhaustive Kirby copies and locked 60 FPS were
already unverified or broken on macOS. Linux validation results and any new
regressions must be reported separately. Omarchy has not been tested on this
Ubuntu machine.

## Bundles, Arch and Omarchy

```sh
python3 native/tools/package_linux.py build/native-linux dist/linux
python3 native/tools/package_arch.py dist/linux/Melee-Native-Linux-x86_64 dist/arch
```

The bundle is relocatable: unpack and run `melee-native`. It requires glibc 2.39+
and a compatible system C++ runtime, Vulkan driver, desktop and audio services.
Packaging copies an explicit executable/library/notice allowlist and verifies
relocated dependencies. It does not copy the build directory or game assets.
The `.pkg.tar.zst` conversion can be inspected with `pacman -Qip` and installed
on Arch with `pacman -U` after review. Generation on Ubuntu does not prove an
Arch installation or an Omarchy/Hyprland session works.

For an Arch-native source build, commit the reviewed changes, run
`python3 native/tools/package_source.py dist/arch`, then use its source-only
archive and checksum-filled `PKGBUILD` with `makepkg -s` on Arch.
The archive contains the committed tree, so uncommitted edits are not included.
Arch needs Clang with compiler-rt, CMake, Ninja, Python, Ruby, pkgconf and the development libraries
listed in the PKGBUILD. On Hyprland use a working `xdg-desktop-portal` setup with
a file chooser backend such as `xdg-desktop-portal-gtk`; the Hyprland portal alone
does not provide every desktop dialog ([Hyprland portal documentation](https://wiki.hypr.land/hyprland-wiki/pages/Useful-Utilities/Hyprland-desktop-portal)).
The desktop launcher is included. Arch runtime dependencies use the current
`libgcc` and `libstdc++` split packages ([Arch GCC package metadata](https://archlinux.org/packages/core/x86_64/gcc-libs/)).

Linux CI and the PKGBUILD use the hardware-validated Debug configuration
(game assertions retained; Aurora renderer compiled with `-O2`). The game
executable is not sanitizer-instrumented; component tests use ASan/UBSan.
Linux CI builds without an image, runs component tests, packages and uploads
bundles and pacman packages as workflow artifacts. CI does not claim real GPU
or Omarchy coverage. Publication and release coordination are separate actions.
