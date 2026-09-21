#!/bin/sh
# One-command PortMaster build: Aurora checkout, SDK/Dawn preparation, AArch64 cross build
# (cortex-a35 baseline so one binary runs on RK3326, RK3566, H700 and newer), packaging.
#
# Inputs (environment; defaults point at the layout the port was developed in):
#   FLIP_TOOLCHAIN    Bootlin aarch64--glibc--stable-2023.08-1 SDK with the device
#                     GLES/GBM/DRM/ALSA libraries pulled in (prepare_flip.py does that)
#   FLIP_DAWN_PREFIX  Dawn install built from native/platform/flip/dawn-gl-interop.patch
#                     (prepare_flip.py builds it into build/flip-tools/dawn-install)
#   RUSTUP_HOME, CARGO_HOME, RUST_TOOLCHAIN   host Rust for Aurora's nod/wgpu build tools
#   MELEE_BUILD_DIR   build tree (default build/portmaster-a35)
#   BUILD_JOBS        parallel jobs (default 6)
#   PORTMASTER_OUTPUT output directory (default dist/portmaster)
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
tools="$root/build/flip-tools"
export RUSTUP_HOME="${RUSTUP_HOME:-$tools/rustup}"
export CARGO_HOME="${CARGO_HOME:-$tools/cargo}"
export PATH="$CARGO_HOME/bin:$PATH"
export MELEE_BUILD_DIR="${MELEE_BUILD_DIR:-$root/build/portmaster-a35}"
# +nocrypto: the ARMv8 crypto ext (AES/SHA/PMULL) is OPTIONAL on Cortex-A35 but clang enables it by default
# for -mcpu=cortex-a35, so abseil emits AES/PMULL/SHA in its hashing. This binary must run on crypto-less
# aarch64 CPUs too (e.g. Raspberry Pi 4 / BCM2711 Cortex-A72: fp/asimd/crc32 but NOT aes) where those
# instructions SIGILL. crc32 stays. See native/platform/flip/toolchain-a35.cmake for the Dawn side.
export FLIP_CPU=cortex-a35+nocrypto
rust_toolchain="${RUST_TOOLCHAIN:-stable-x86_64-unknown-linux-gnu}"
output="${PORTMASTER_OUTPUT:-$root/dist/portmaster}"

echo "== Aurora (zalo/aurora-arm gles-direct-submission)"
python3 "$root/native/tools/bootstrap.py"

if [ -z "${FLIP_TOOLCHAIN:-}" ] || [ -z "${FLIP_DAWN_PREFIX:-}" ]; then
    # PREPARE_FLIP_ARGS=--no-device takes the device libraries from Debian arm64 packages (CI);
    # otherwise the device must be on ADB once for its GLES libraries.
    echo "== Preparing the SDK, device libraries and the cortex-a35 Dawn"
    python3 "$root/native/tools/prepare_flip.py" --cpu a35 ${PREPARE_FLIP_ARGS:-}
    : "${FLIP_TOOLCHAIN:=$tools/aarch64--glibc--stable-2023.08-1}"
    : "${FLIP_DAWN_PREFIX:=$tools/dawn-install-a35}"
fi
export FLIP_TOOLCHAIN FLIP_DAWN_PREFIX
sdk=$FLIP_TOOLCHAIN

if [ ! -x "$CARGO_HOME/bin/rustup" ]; then
    echo "rustup not found at $CARGO_HOME/bin/rustup; install it with RUSTUP_HOME/CARGO_HOME under $tools" >&2
    exit 1
fi

# port.json promises min_glibc 2.30 (ArkOS), but the SDK's glibc is 2.37. The release link therefore
# uses a copy of the SDK whose sysroot carries glibc 2.30 (native/tools/glibc230_toolchain.sh).
# MELEE_MIN_GLIBC=sdk skips that and links against the SDK as is (needs glibc 2.34+ on the device).
link_flags=
if [ "${MELEE_MIN_GLIBC:-2.30}" != "sdk" ]; then
    hybrid="${FLIP_TOOLCHAIN_GLIBC230:-$tools/aarch64--glibc-2.30-hybrid}"
    # Returns at once when the hybrid exists with the current layout, rebuilds an outdated one.
    sh "$root/native/tools/glibc230_toolchain.sh" build "$sdk" "$hybrid"
    export FLIP_TOOLCHAIN="$hybrid"
    # The SDK's libdrm/libz link stubs reference glibc 2.33/2.34 symbols; the device's own copies are loaded at run time.
    link_flags="-DCMAKE_EXE_LINKER_FLAGS=-Wl,--allow-shlib-undefined"
fi

# SDL: the package links SDL3 as a shared library and ships bmdhacks' SDL3-over-SDL2 shim as
# libs.aarch64/libSDL3.so.0 (MELEE_SDL=shim, the default; see native/tools/build_sdl3_shim.sh), so the
# CFW's own SDL2 owns the display. MELEE_SDL=static builds SDL3 in (KMSDRM/Wayland only; no fbdev CFWs).
export MELEE_SDL="${MELEE_SDL:-shim}"
package_sdl3=
if [ "$MELEE_SDL" = shim ]; then
    export FLIP_SDL3_ROOT="${FLIP_SDL3_ROOT:-$tools/sdl3-shim-install}"
    echo "== SDL3-over-SDL2 shim ($FLIP_SDL3_ROOT)"
    sh "$root/native/tools/build_sdl3_shim.sh" "$FLIP_SDL3_ROOT"
    package_sdl3="--sdl3 $FLIP_SDL3_ROOT"
fi

echo "== Cross build (melee_native for AArch64, -mcpu=$FLIP_CPU, toolchain $FLIP_TOOLCHAIN, SDL $MELEE_SDL)"
# Mali-G31 (RK3326) reports the GLES minimum GL_MAX_UNIFORM_BLOCK_SIZE of 16 KiB, so the uniform
# window must be 16 KiB (Aurora's default is 64); the vertex stream is Aurora's default 5 MiB.
# The expected Aurora revision is a CMake cache variable, so a build tree configured before a pin
# bump would keep the old value and fail the revision check; pass bootstrap.py's pin explicitly.
aurora_rev=$(sed -n "s/^revision = '\([0-9a-f]*\)'.*/\1/p" "$root/native/tools/bootstrap.py")
sh "$root/native/platform/flip/build.sh" \
    -DRust_RUSTUP="$CARGO_HOME/bin/rustup" -DRust_TOOLCHAIN="$rust_toolchain" \
    -DMELEE_AURORA_EXPECTED_REV="$aurora_rev" \
    -DAURORA_UNIFORM_WINDOW_KIB=16 -DAURORA_VERTEX_BUFFER_MIB=5 ${link_flags:+"$link_flags"} "$@"

if [ -n "$link_flags" ]; then
    echo "== glibc check"
    sh "$root/native/tools/glibc230_toolchain.sh" verify "$sdk" "$MELEE_BUILD_DIR/melee_native"
    if [ "$MELEE_SDL" = shim ]; then
        sh "$root/native/tools/glibc230_toolchain.sh" verify "$sdk" "$FLIP_SDL3_ROOT/lib/libSDL3.so.0"
    fi
fi

echo "== Packaging"
rm -rf "$output/melee" "$output/melee.zip"
# The plain SDK: package_flip.py strips with it and reads its libstdc++.so.6 for the Flip bundle
# (dropped again for PortMaster); the glibc 2.30 toolchain has no shared libstdc++.
python3 "$root/native/tools/package_portmaster.py" --build "$MELEE_BUILD_DIR" --sdk "$sdk" --output "$output" $package_sdl3
if [ "$MELEE_SDL" = shim ]; then
    sh "$root/native/platform/flip/check_sdl_backends.sh" --shim "$output/melee/melee/libs.aarch64/libSDL3.so.0" "$output/melee/melee/melee.aarch64"
else
    sh "$root/native/platform/flip/check_sdl_backends.sh" "$MELEE_BUILD_DIR" "$output/melee/melee/melee.aarch64"
fi
stamp=$(git -C "$root" rev-parse --short HEAD 2>/dev/null || echo local)
mkdir -p "$tools"
cp "$MELEE_BUILD_DIR/melee_native" "$tools/melee_native-portmaster-$stamp-symbols"
echo "symbols: $tools/melee_native-portmaster-$stamp-symbols"
sha256sum "$output/melee.zip"
