#!/bin/sh
# Fail unless the SDL the port runs on can reach every CFW display and audio stack.
#
#   check_sdl_backends.sh <build-tree> [<melee binary>]              static SDL3 build
#   check_sdl_backends.sh --shim <libSDL3.so.0> <melee binary>       SDL3-over-SDL2 shim build
#
# Static mode: SDL compiles a backend only when pkg-config finds its module and drops it silently
# otherwise, while the cache option (SDL_KMSDRM=ON) stays ON. One release shipped with the Wayland
# driver only, which is "No available video device" on every KMSDRM CFW. This reads what SDL actually
# generated, and with a binary also checks the driver names inside it.
# Shim mode: the game must link libSDL3.so.0 dynamically (no SDL3 built in) and the shipped shim must
# carry its "sdl2" driver, which dlopens the CFW's libSDL2-2.0.so.0.
set -eu
[ $# -ge 1 ] || { echo "usage: check_sdl_backends.sh <build-tree> [<binary>] | --shim <libSDL3.so.0> <binary>" >&2; exit 2; }
status=0
has_string() { strings -n 5 "$1" | grep -qx "$2"; }

if [ "$1" = --shim ]; then
    shim=${2:?shim library}
    binary=${3:?melee binary}
    # "sdl2" is the shim driver's name (four characters, so strings -n 4).
    strings -n 4 "$shim" | grep -qx sdl2 || { echo "check_sdl_backends: $shim has no 'sdl2' driver (not the SDL2-backend shim?)" >&2; status=1; }
    for name in SDL3SHIM_SDL2_LIB SDL3SHIM_SDL2_VIDEODRIVER libSDL2-2.0.so.0; do
        has_string "$shim" "$name" || { echo "check_sdl_backends: $shim lacks '$name' (not the SDL2-backend shim?)" >&2; status=1; }
    done
    # SDL3's own Linux drivers must be absent from the shim: their hint names only exist when compiled in.
    for name in SDL_KMSDRM_DEVICE_INDEX SDL_VIDEO_WAYLAND_ALLOW_LIBDECOR SDL_VIDEO_X11_NET_WM_PING; do
        if has_string "$shim" "$name"; then
            echo "check_sdl_backends: $shim carries SDL3's own video driver ($name); it must delegate to SDL2 only" >&2; status=1
        fi
    done
    readelf -d "$binary" | grep -q 'Shared library: \[libSDL3.so.0\]' \
        || { echo "check_sdl_backends: $binary does not link libSDL3.so.0 (readelf -d NEEDED)" >&2; status=1; }
    if has_string "$binary" SDL_KMSDRM_DEVICE_INDEX; then
        echo "check_sdl_backends: $binary has SDL3's KMSDRM driver built in; the shim build must link SDL3 dynamically" >&2; status=1
    fi
    # The display stack is the CFW's SDL2's business: the binary may require only the GL driver
    # (libEGL/libGLESv2), libSDL3.so.0 and libc-level libraries, never libdrm/libgbm/libwayland, or it
    # would not even load on a CFW whose SDL2 runs on fbdev.
    for name in libdrm.so.2 libgbm.so.1 libwayland-client.so.0 libwayland-egl.so.1 libSDL2-2.0.so.0; do
        if readelf -d "$binary" | grep -q "Shared library: \[$name\]"; then
            echo "check_sdl_backends: $binary requires $name (readelf -d NEEDED); the display stack must come through SDL2 only" >&2; status=1
        fi
    done
    # Math must not come from the CFW: two devices with different glibc versions computed different
    # fighter physics and desynced online play. The sysroot's libm.a is linked in instead.
    if readelf -d "$binary" | grep -q 'Shared library: \[libm.so.6\]'; then
        echo "check_sdl_backends: $binary requires libm.so.6; link the sysroot's libm.a (online play needs identical math on every device)" >&2; status=1
    fi
    for name in sinf cosf atan2f sqrtf; do
        if readelf --dyn-syms -W "$binary" | grep -q " UND ${name}@"; then
            echo "check_sdl_backends: $binary imports $name from the device's libm" >&2; status=1
        fi
    done
    [ $status -eq 0 ] && echo "SDL: shared libSDL3.so.0 (SDL2-backend shim), display and audio through the CFW's SDL2"
    exit $status
fi

build=$1
binary=${2:-}
config=$(find "$build/_deps/sdl-build" -name SDL_build_config.h -path '*build_config*' 2>/dev/null | head -n 1)
[ -n "$config" ] || { echo "check_sdl_backends: no generated SDL_build_config.h under $build/_deps/sdl-build" >&2; exit 1; }
for define in SDL_VIDEO_DRIVER_KMSDRM SDL_VIDEO_DRIVER_KMSDRM_DYNAMIC \
              SDL_VIDEO_DRIVER_WAYLAND SDL_VIDEO_DRIVER_WAYLAND_DYNAMIC \
              SDL_AUDIO_DRIVER_ALSA SDL_AUDIO_DRIVER_ALSA_DYNAMIC \
              SDL_AUDIO_DRIVER_PULSEAUDIO SDL_AUDIO_DRIVER_PULSEAUDIO_DYNAMIC \
              SDL_AUDIO_DRIVER_PIPEWIRE SDL_AUDIO_DRIVER_PIPEWIRE_DYNAMIC; do
    if ! grep -q "^#define $define " "$config"; then
        echo "check_sdl_backends: SDL was configured without $define ($config)" >&2
        status=1
    fi
done
if [ -n "$binary" ]; then
    for name in kmsdrm wayland libdrm.so.2 libgbm.so.1 libwayland-client.so.0 \
                libasound.so.2 libpulse.so.0 libpipewire-0.3.so.0; do
        has_string "$binary" "$name" || { echo "check_sdl_backends: $binary does not contain '$name'" >&2; status=1; }
    done
fi
[ $status -eq 0 ] && echo "SDL backends present: kmsdrm + wayland (video), alsa + pulseaudio + pipewire (audio), all loaded at run time"
exit $status
