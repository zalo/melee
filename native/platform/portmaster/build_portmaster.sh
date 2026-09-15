#!/bin/sh
# One-command PortMaster build: Aurora checkout, Flip SDK/Dawn preparation,
# AArch64 cross build, PortMaster packaging.
#
# Inputs (environment; defaults point at the layout the port was developed in):
#   FLIP_TOOLCHAIN    Bootlin aarch64--glibc--stable-2023.08-1 SDK with the device
#                     GLES/GBM/DRM/ALSA libraries pulled in (prepare_flip.py does that)
#   FLIP_DAWN_PREFIX  Dawn install built from native/platform/flip/dawn-gl-interop.patch
#                     (prepare_flip.py builds it into build/flip-tools/dawn-install)
#   RUSTUP_HOME, CARGO_HOME, RUST_TOOLCHAIN   host Rust for Aurora's nod/wgpu build tools
#   MALI_G29          optional libmali.so.1 (g29p1) to bundle for RK3566 devices
#   BUILD_JOBS        parallel jobs (default 6)
#   PORTMASTER_OUTPUT output directory (default dist/portmaster)
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
tools="$root/build/flip-tools"
export RUSTUP_HOME="${RUSTUP_HOME:-$tools/rustup}"
export CARGO_HOME="${CARGO_HOME:-$tools/cargo}"
export PATH="$CARGO_HOME/bin:$PATH"
rust_toolchain="${RUST_TOOLCHAIN:-stable-x86_64-unknown-linux-gnu}"
output="${PORTMASTER_OUTPUT:-$root/dist/portmaster}"

echo "== Aurora (zalo/aurora-arm gles-direct-submission)"
python3 "$root/native/tools/bootstrap.py"

if [ -z "${FLIP_TOOLCHAIN:-}" ] || [ -z "${FLIP_DAWN_PREFIX:-}" ]; then
    echo "== Preparing the SDK and Dawn (needs the device on ADB once, for its GLES libraries)"
    python3 "$root/native/tools/prepare_flip.py" ${PREPARE_FLIP_ARGS:-}
    : "${FLIP_TOOLCHAIN:=$tools/aarch64--glibc--stable-2023.08-1}"
    : "${FLIP_DAWN_PREFIX:=$tools/dawn-install}"
fi
export FLIP_TOOLCHAIN FLIP_DAWN_PREFIX

if [ ! -x "$CARGO_HOME/bin/rustup" ]; then
    echo "rustup not found at $CARGO_HOME/bin/rustup; install it with RUSTUP_HOME/CARGO_HOME under $tools" >&2
    exit 1
fi

echo "== Cross build (melee_native for AArch64)"
sh "$root/native/platform/flip/build.sh" \
    -DRust_RUSTUP="$CARGO_HOME/bin/rustup" -DRust_TOOLCHAIN="$rust_toolchain" "$@"

echo "== Packaging"
rm -rf "$output/melee" "$output/melee.zip"
set -- --output "$output"
if [ -n "${MALI_G29:-}" ]; then set -- "$@" --mali-g29 "$MALI_G29"; fi
python3 "$root/native/tools/package_portmaster.py" "$@"
stamp=$(git -C "$root" rev-parse --short HEAD 2>/dev/null || echo local)
mkdir -p "$tools"
cp "$root/build/native-flip/melee_native" "$tools/melee_native-portmaster-$stamp-symbols"
echo "symbols: $tools/melee_native-portmaster-$stamp-symbols"
sha256sum "$output/melee.zip"
