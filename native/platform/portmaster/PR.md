# Pull request notes (not shipped in the port)

Target repository: PortsMaster-MV/PortMaster-MV-New ("ports unable to be included in the main
repositories"; Dusklight, the Twilight Princess decompilation port on the same Aurora renderer, lives
there). Its PR template is reproduced below, filled in. The main repository's AGENTS.md rules were
followed anyway, and its reviewers ask about every launch-script line that does not appear in a
comparable merged port, so the explanations further down are worth keeping in the PR.

# New Port for Super Smash Bros. Melee (native)

## Game Information
- **Title**: Super Smash Bros. Melee (native)
- **URL**: https://github.com/zalo/melee/tree/portmaster (game code: https://github.com/doldecomp/melee,
  renderer: https://github.com/encounter/aurora)

## Submission Requirements

### CFW Tests
Ensure your game has been tested on all major CFWs:
- [x] AmberELEC (RG351P)
- [ ] ArkOS (untested; the binary needs only glibc 2.30 for it. dArkOS, its RG351MP derivative, passes)
- [x] ROCKNIX (RG351P and Miyoo Flip, libmali and Panfrost)
- [ ] muOS (not supported yet: its fbdev libmali stack has no KMSDRM or Wayland display)
- [x] Also: Knulli (Miyoo Flip), dArkOS (RG351P), Batocera 42 (RG351P)

### Resolution Tests
Test all major resolutions:
- [x] 480x320 (RG351P)
- [x] 640x480 (Miyoo Flip)
- [ ] 720x720 (RGB30)
- [ ] Higher resolutions (e.g., 1280x720)

## File Structure
ports/melee/: port.json, README.md, screenshot.jpg, cover.jpg, gameinfo.xml, Melee.sh,
melee/ (melee.aarch64, melee.ini, licenses/, assets/README.txt, screenshot.jpg).

## Authorship
The launch script, packaging scripts and the native platform code were written with an AI assistant
(Claude Code) and reviewed line by line; the explanations below are the reviewers' checklist.

## Script Conventions: the non-standard lines in Melee.sh and why

- `XDG_CONFIG_HOME` / `XDG_STATE_HOME` / `XDG_CACHE_HOME` exported into `melee/runtime/`: the game honours
  the XDG directories, so its memory card, logs and pipeline cache stay inside the port folder
  instead of `~/.config` and `~/.cache`, which several CFWs keep on a small or volatile partition. No
  `bind_directories` is needed because nothing is symlinked.
- `if [ ${#sdl_controllerconfig} -lt 100000 ]`: Batocera's control.txt puts the whole
  gamecontrollerdb.txt (472 KB) into `sdl_controllerconfig`; exporting it exceeds the 128 KiB limit on
  one environment string and every following exec fails with "Argument list too long". The game reads
  the pad through SDL's own database when the variable is absent.
- `shopt -s nullglob nocaseglob` and the `discs` array: the user's disc image may be `.iso`, `.gcm`,
  `.ciso` or `.rvz` in any case; the first match is passed to the binary.
- `chmod +x "$GAMEDIR/melee.aarch64"`: zip extraction drops the exec bit.
- `$GPTOKEYB2 "melee.aarch64" -c "$GAMEDIR/melee.ini"` with an empty `[controls]` section: the game reads
  the controller itself; gptokeyb2 only provides the Start+Select exit hotkey.
- The two `pm_message` calls: one when no disc image is present, one when the binary exits because
  no OpenGL ES 3.1 driver or display could be opened, so the user sees the reason instead of a
  silent return to the menu.

## Build choices the guide warns about

- The C++ runtime is linked statically. The game is C++20 and needs GLIBCXX_3.4.30 or newer; several
  CFWs still ship 3.4.28, and PortMaster forbids bundling libstdc++ in `libs.aarch64`. Static
  linking is the remaining option and is what makes one binary run on AmberELEC, Batocera, dArkOS,
  Knulli and ROCKNIX.
- `libs.aarch64/libSDL3.so.0` is the SDL3-over-SDL2 shim (bmdhacks/SDL, branch `sdl2-backend`),
  the same library Dusklight ships. The renderer (Aurora) is written against SDL3; the CFWs ship
  SDL2. The shim's video, audio and joystick drivers dlopen the CFW's own `libSDL2-2.0.so.0`, so the
  device's display path (KMSDRM, fbdev, Wayland), audio server and pad quirks all come from the CFW,
  and no SDL2 is bundled or shadowed. `Melee.sh` hands the CFW's `SDL_VIDEODRIVER` /
  `SDL_AUDIODRIVER` to the inner SDL2 through `SDL3SHIM_SDL2_VIDEODRIVER` / `_AUDIODRIVER` and sets
  SDL3's own drivers to `sdl2`, as Dusklight's launcher does.
- No GPU driver is bundled. libEGL and libGLESv2 are linked through generated stubs with the standard
  sonames and resolved from the device at run time; the port therefore works on libmali and on Mesa
  (Panfrost).
- `min_glibc` is 2.30: the release is linked against a glibc 2.30 sysroot and checked at build time
  (see README.md, Building).
