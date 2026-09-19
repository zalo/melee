#!/bin/sh
# Build the SDL3-over-SDL2 shim the PortMaster package ships as libs.aarch64/libSDL3.so.0.
#
#   build_sdl3_shim.sh <install prefix> [<source checkout>] [<build tree>]
#
# The shim is bmdhacks' SDL fork, branch sdl2-backend (the library Dusklight ships): a real SDL3
# whose "sdl2" video, audio and joystick drivers dlopen the CFW's own libSDL2-2.0.so.0 at run time.
# The game links SDL3 as a shared library, so every CFW keeps its patched SDL2 display path (KMSDRM,
# fbdev, Wayland), and the shim never shadows the CFW's SDL2. Built with FLIP_TOOLCHAIN (the glibc
# 2.30 hybrid for releases) and -mcpu=cortex-a35 like the game. SDL_GPU is off: the game renders
# through Dawn, and the shim's GPU backend would need SPIRV-Cross.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../.." && pwd)
prefix=${1:?usage: build_sdl3_shim.sh <install prefix> [<source checkout>] [<build tree>]}
src=${2:-$root/build/native-deps/sdl3-shim}
build=${3:-$root/build/sdl3-shim-a35}
: "${FLIP_TOOLCHAIN:?Set FLIP_TOOLCHAIN to the AArch64 GNU SDK (the glibc 2.30 hybrid for releases)}"
repo=https://github.com/bmdhacks/SDL.git
branch=sdl2-backend
# Pinned commit ("SDL2 backend GPU: statically link SPIRV-Cross + libstdc++", 2026-05-18, SDL 3.5.0).
rev=6057d79baf8321bf190479a699655f06cc2a962f
layout=1

if [ -f "$prefix/lib/libSDL3.so.0" ] && [ "$(cat "$prefix/.melee-shim" 2>/dev/null)" = "$rev-$layout" ]; then
    echo "SDL3 shim present at $prefix ($rev)"
    exit 0
fi
if [ ! -d "$src/.git" ]; then
    git clone -q --branch "$branch" "$repo" "$src"
fi
if [ "$(git -C "$src" rev-parse HEAD)" != "$rev" ]; then
    git -C "$src" fetch -q origin "$branch"
    git -C "$src" checkout -q "$rev"
fi
sysroot="$FLIP_TOOLCHAIN/aarch64-buildroot-linux-gnu/sysroot"
export PKG_CONFIG_LIBDIR="$sysroot/usr/lib/pkgconfig:$sysroot/usr/share/pkgconfig"
unset PKG_CONFIG_PATH
cmake -S "$src" -B "$build" -DCMAKE_TOOLCHAIN_FILE="$root/native/platform/flip/toolchain-a35.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_SDL2_BACKEND=ON -DSDL_UNIX_CONSOLE_BUILD=ON \
    -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF -DSDL_OFFSCREEN=OFF -DSDL_DUMMYVIDEO=OFF \
    -DSDL_PIPEWIRE=OFF -DSDL_PULSEAUDIO=OFF -DSDL_ALSA=OFF -DSDL_SNDIO=OFF -DSDL_OSS=OFF -DSDL_JACK=OFF \
    -DSDL_DUMMYAUDIO=OFF -DSDL_DISKAUDIO=OFF -DSDL_VULKAN=OFF -DSDL_OPENGL=OFF -DSDL_OPENGLES=ON \
    -DSDL_GPU=OFF -DSDL_RENDER_GPU=OFF -DSDL_HIDAPI=OFF \
    -DSDL_TESTS=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL_DOCS=OFF
cmake --build "$build" --parallel "${BUILD_JOBS:-6}"
rm -rf "$prefix"
cmake --install "$build" > /dev/null
cp "$src/LICENSE.txt" "$prefix/LICENSE.txt"
echo "$rev-$layout" > "$prefix/.melee-shim"
echo "SDL3 shim: $prefix/lib/libSDL3.so.0 ($rev)"
