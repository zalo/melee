#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
: "${FLIP_TOOLCHAIN:?Set FLIP_TOOLCHAIN to the AArch64 GNU SDK}"
: "${FLIP_DAWN_PREFIX:?Set FLIP_DAWN_PREFIX to the patched Dawn install directory}"
cmake -S "$root/native" -B "$root/build/native-flip-a35" \
    -DCMAKE_TOOLCHAIN_FILE="$root/native/platform/flip/toolchain-a35.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DMELEE_MIYOO_FLIP=ON \
    -DAURORA_DAWN_PROVIDER=system -DDawn_DIR="$FLIP_DAWN_PREFIX/lib/cmake/Dawn" \
    -DAURORA_NOD_PROVIDER=vendor -DAURORA_GLES_DIRECT_DAWN_INCLUDE_DIR="$FLIP_DAWN_PREFIX/include" -DRust_CARGO_TARGET=aarch64-unknown-linux-gnu \
    -DBUILD_SHARED_LIBS=OFF -DSDL_UNIX_CONSOLE_BUILD=ON \
    -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=ON \
    -DSDL_OPENGL=OFF -DSDL_OPENGLES=ON -DSDL_VULKAN=OFF \
    -DSDL_ALSA=ON -DSDL_ALSA_SHARED=ON -DSDL_PULSEAUDIO=OFF \
    -DSDL_PIPEWIRE=OFF -DSDL_JACK=OFF -DSDL_SNDIO=OFF \
    -DAURORA_CACHE_USE_ZSTD=OFF -DTRACY_ENABLE=OFF "$@"
cmake --build "$root/build/native-flip-a35" --target melee_native --parallel "${BUILD_JOBS:-6}"
