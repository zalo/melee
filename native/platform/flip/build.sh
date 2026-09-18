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
export PKG_CONFIG_PATH="$wayland/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
cmake -S "$root/native" -B "$build" \
    -DCMAKE_TOOLCHAIN_FILE="$root/native/platform/flip/toolchain.cmake" \
    -DCMAKE_C_FLAGS="-mcpu=$cpu" -DCMAKE_CXX_FLAGS="-mcpu=$cpu" \
    -DCMAKE_BUILD_TYPE=Release -DMELEE_MIYOO_FLIP=ON \
    -DAURORA_DAWN_PROVIDER=system -DDawn_DIR="$FLIP_DAWN_PREFIX/lib/cmake/Dawn" \
    -DAURORA_NOD_PROVIDER=vendor -DAURORA_GLES_DIRECT_DAWN_INCLUDE_DIR="$FLIP_DAWN_PREFIX/include" -DRust_CARGO_TARGET=aarch64-unknown-linux-gnu \
    -DBUILD_SHARED_LIBS=OFF -DSDL_UNIX_CONSOLE_BUILD=ON \
    -DSDL_X11=OFF -DSDL_KMSDRM=ON -DFLIP_WAYLAND_ROOT="$FLIP_WAYLAND_ROOT" \
    -DSDL_WAYLAND=ON -DSDL_WAYLAND_SHARED=ON -DSDL_WAYLAND_LIBDECOR=OFF \
    -DSDL_OPENGL=OFF -DSDL_OPENGLES=ON -DSDL_VULKAN=OFF \
    -DSDL_ALSA=ON -DSDL_ALSA_SHARED=ON -DSDL_PULSEAUDIO=OFF \
    -DSDL_PIPEWIRE=OFF -DSDL_JACK=OFF -DSDL_SNDIO=OFF \
    -DAURORA_CACHE_USE_ZSTD=OFF -DTRACY_ENABLE=OFF "$@"
cmake --build "$build" --target melee_native --parallel "${BUILD_JOBS:-6}"
