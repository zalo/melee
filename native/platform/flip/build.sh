#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
: "${FLIP_TOOLCHAIN:?Set FLIP_TOOLCHAIN to the AArch64 GNU SDK}"
: "${FLIP_DAWN_PREFIX:?Set FLIP_DAWN_PREFIX to the patched Dawn install directory}"
# MELEE_BUILD_DIR and FLIP_CPU select the build tree and -mcpu (cortex-a35 runs on every
# AArch64 handheld; cortex-a55 is the Flip-only tuning).
build="${MELEE_BUILD_DIR:-$root/build/native-flip}"
cpu="${FLIP_CPU:-cortex-a55}"
# SDL's Wayland driver (ROCKNIX) needs AArch64 wayland/xkbcommon .pc files and sonames at configure
# time; native/tools/prepare_wayland.py fetches them. SDL dlopens the libraries, so nothing is linked.
wayland="${FLIP_WAYLAND_DIR:-$root/build/flip-tools/wayland-arm64}"
[ -d "$wayland/pkgconfig" ] || python3 "$root/native/tools/prepare_wayland.py" --output "$wayland"
export FLIP_WAYLAND_ROOT="$wayland/root"
# pkg-config may only see target packages: Aurora asks it for sqlite3 (and zstd) before fetching its
# pinned copies, and the host's x86_64 sqlite3 answered in the cross build (headers from /usr/include,
# library not found). PKG_CONFIG_LIBDIR replaces the default search path; PKG_CONFIG_PATH would add to it.
sysroot="$FLIP_TOOLCHAIN/aarch64-buildroot-linux-gnu/sysroot"
export PKG_CONFIG_LIBDIR="$wayland/pkgconfig:$sysroot/usr/lib/pkgconfig:$sysroot/usr/share/pkgconfig"
unset PKG_CONFIG_PATH
# MELEE_SDL=shim (the PortMaster default) links SDL3 as a shared library and ships bmdhacks' SDL3-over-SDL2
# shim (native/tools/build_sdl3_shim.sh) so the CFW's own SDL2 owns the display; MELEE_SDL=static builds
# SDL3 in with its KMSDRM/Wayland drivers (the Flip development flow).
sdl_mode="${MELEE_SDL:-static}"
case "$sdl_mode" in
    shim)
        : "${FLIP_SDL3_ROOT:?MELEE_SDL=shim needs FLIP_SDL3_ROOT (build_sdl3_shim.sh install prefix)}"
        export FLIP_SDL3_ROOT
        sdl_args="-DAURORA_SDL3_PROVIDER=system -DAURORA_SDL3_LINKAGE=shared -DSDL3_DIR=$FLIP_SDL3_ROOT/lib/cmake/SDL3 -DFLIP_SDL3_ROOT=$FLIP_SDL3_ROOT" ;;
    static)
        sdl_args="-DAURORA_SDL3_PROVIDER=vendor -DAURORA_SDL3_LINKAGE=static -DSDL_UNIX_CONSOLE_BUILD=ON
            -DSDL_X11=OFF -DSDL_KMSDRM=ON -DSDL_KMSDRM_SHARED=ON
            -DSDL_WAYLAND=ON -DSDL_WAYLAND_SHARED=ON -DSDL_WAYLAND_LIBDECOR=OFF
            -DSDL_OPENGL=OFF -DSDL_OPENGLES=ON -DSDL_VULKAN=OFF
            -DSDL_ALSA=ON -DSDL_ALSA_SHARED=ON -DSDL_PULSEAUDIO=ON -DSDL_PULSEAUDIO_SHARED=ON
            -DSDL_PIPEWIRE=ON -DSDL_PIPEWIRE_SHARED=ON -DSDL_JACK=OFF -DSDL_SNDIO=OFF" ;;
    *) echo "MELEE_SDL must be shim or static" >&2; exit 2 ;;
esac
# SDL only compiles a backend whose pkg-config module it finds, and drops it silently otherwise.
# A release once shipped without the KMSDRM driver because the sysroot got its libdrm/gbm .pc files
# after the tree was configured: write the missing ones first, then refuse to build without them.
python3 -c "import sys, pathlib; sys.path.insert(0, '$root/native/tools'); import prepare_flip; prepare_flip.write_sysroot_pkgconfig(pathlib.Path('$sysroot/usr'))"
cmake -S "$root/native" -B "$build" \
    -DCMAKE_TOOLCHAIN_FILE="$root/native/platform/flip/toolchain.cmake" \
    -DCMAKE_C_FLAGS="-mcpu=$cpu" -DCMAKE_CXX_FLAGS="-mcpu=$cpu" \
    -DCMAKE_BUILD_TYPE=Release -DMELEE_MIYOO_FLIP=ON \
    -DAURORA_DAWN_PROVIDER=system -DDawn_DIR="$FLIP_DAWN_PREFIX/lib/cmake/Dawn" \
    -DAURORA_NOD_PROVIDER=vendor -DAURORA_GLES_DIRECT_DAWN_INCLUDE_DIR="$FLIP_DAWN_PREFIX/include" -DRust_CARGO_TARGET=aarch64-unknown-linux-gnu \
    -DBUILD_SHARED_LIBS=OFF -DFLIP_WAYLAND_ROOT="$FLIP_WAYLAND_ROOT" $sdl_args \
    -DAURORA_CACHE_USE_ZSTD=OFF -DTRACY_ENABLE=OFF "$@"
if [ "$sdl_mode" = static ]; then
    sh "$root/native/platform/flip/check_sdl_backends.sh" "$build"
fi
cmake --build "$build" --target melee_native --parallel "${BUILD_JOBS:-6}"
