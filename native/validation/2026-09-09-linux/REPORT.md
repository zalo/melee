# Ubuntu ThinkPad validation — 2026-09-09

Native x86-64 Linux gameplay works on this ThinkPad T480s, Ubuntu 24.04.4,
GNOME X11, i7-8550U, with Aurora/Dawn Vulkan and SDL3. No emulator is involved.
Original upstream remains [doldecomp/melee](https://github.com/doldecomp/melee).

## Build and coverage

- Clang 18 Debug game, Aurora renderer `-O2`, pinned Aurora `749d6ee7a22bdfab78c8ece9047bca5d79aa72ca`.
- Source builds without an image. All 18 asset-free component tests pass; component tests use ASan/UBSan, the game executable does not. Optional boot probe also builds.
- Intel UHD620, Mesa ANV 25.2.8: **35/35 combined cases pass**, covering **29 VS stages, 26 character entries, 35 common item kinds**. [Numeric evidence](matrix.json).
- One extended timed match follows normal timeout/sudden-death handling through Results and returns to character selection. Other cases sample 900 frames each with changing CPU fighters and repeated item spawns.
- Render acceptance excludes the first countdown sample and requires at least three and 80% varied combat samples: at least eight colors, dominant color below 99.5%, edge variation above 0.005. Finite camera checks remain active. Solid blue/nonblack alone does not pass.
- Real SDL keyboard events exercised the portal image chooser, cancel, invalid image error, retry, opening/title/menus and character selection. Reused PAD scripts exercised stage selection, movement, jump, attacks, special, shield, grab, C-stick and pause/resume in a real match.
- SDL reports PulseAudio and Built-in Audio Analog Stereo; nonzero mixed samples and active PipeWire links to both physical ALC257 playback channels were verified at modest volume. No saved screenshots or videos.

The tested unpackaged executable SHA256 is
`4a7e43df13b7fe807f1a05daad1ce82881e47a1025ba63377f6641b2477fc2f8`.
Packaging changes the ELF RPATH; the bundle manifest records that resulting hash.

## Performance and GPU selection

The full matrix explicitly used `/usr/share/vulkan/icd.d/intel_icd.json`.
Across 111 five-second gameplay measurements, FPS ranged 53.50–60.04,
median 59.25. Excluding each case's first measurement leaves 76 samples,
58.13–60.04 FPS, median 59.55. Across those later windows, median frame
intervals ranged 16.606–16.725 ms; p95 17.363–20.481 ms; p99 18.046–37.053 ms;
maximum observed interval 66.496 ms. Initial scene/shader stalls are larger.
These are presentation measurements, not proof of locked 60 FPS.

Default adapter selection chooses NVIDIA MX150 through NVK Mesa 25.2.8.
The final bundle was unpacked into a path containing spaces and launched from
an empty working directory with game-process library overrides removed. It
completed the match-control sequence with varying playfield rendering and
nonzero audio. Five-second gameplay samples were 45.61, 42.19, 42.20, 42.12,
42.87 FPS ([evidence](relocated-nvk.json)). This is hardware rendering but
substantially slower than Intel on this system. Use the explicit Intel command
in [LINUX.md](../../LINUX.md) for the validated faster path. No drivers or OS
packages were modified; missing build tools were extracted into a local sysroot.

The relocated bundle also passed the picker cancel/error/retry and physical
keyboard/audio checks on Intel with a clean game library environment
([desktop evidence](desktop.json)). Only README text changed in the final
repackaging; executable and library hashes match the relocated tested bundle.

## Fixes and limits

Linux exposed actual crashes from console assumptions: integer-zero varargs
sentinels on x86-64, independently ordered card-work and item-drop globals,
and insufficient movie DVD-buffer alignment. These were corrected alongside
Linux file dialogs, XDG paths, stack-bound queries, logging and stdio adapters.
The item-drop bug now has a focused regression test. Card work-area separation
fixes idle polling; it does **not** implement complete saving.

Short-match coverage does not certify every move/effect, costume, matchup,
mode, Kirby copy or Pokemon variant. Saving, adventure/target stages,
exhaustive Kirby copies and locked 60 FPS were already incomplete or unverified
on macOS. They remain outside this acceptance. macOS source/bundle conditionals
and Xcode 26.2 CI were preserved; actual Mac regression checks remain for the
parent. Omarchy/Arch installation and Hyprland/Wayland runtime have **not** been
tested on this Ubuntu host.

## Local artifacts and reproducibility

`dist/linux/Melee-Native-Linux-x86_64.tar.gz` is an allowlisted relocatable
runtime bundle with dependency notices. `dist/arch/melee-native-0.1.0-1-x86_64.pkg.tar.zst`
has been read successfully by libalpm/pacman. The source archive and
checksum-filled PKGBUILD are generated from the committed source tree.
No disc, extracted game assets, font atlases, firmware or builds are committed.
Both font atlases continue to load from the selected private image at runtime.

Linux CI builds source-only, tests, packages and uploads downloadable workflow
artifacts. It has been configured locally, not executed remotely. Nothing has
been pushed or published. Local commands and raw log paths are recorded in
`build/linux-task/status.md`; matrix case logs are referenced in the numeric
report. See [LINUX.md](../../LINUX.md) for build and packaging commands.
