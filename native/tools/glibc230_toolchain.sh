#!/bin/sh
# Build the glibc 2.30 cross toolchain the PortMaster binary is linked with, and verify a binary
# against it.
#
#   glibc230_toolchain.sh build   <sdk> <out>        make the hybrid toolchain at <out> from <sdk>
#   glibc230_toolchain.sh verify  <sdk> <binary>     fail unless <binary> needs at most GLIBC_2.30
#
# <sdk> is the Bootlin aarch64--glibc--stable-2023.08-1 toolchain that prepare_flip.py filled with the
# device's GLES/GBM/DRM/ALSA libraries. Its glibc is 2.37, so a binary linked against it needs
# GLIBC_2.34+ and cannot start on ArkOS (glibc 2.30) or CrossMix (2.33). port.json promises
# min_glibc 2.30, so the release link uses the same compiler, gcc 12 runtime and prebuilt Dawn but a
# sysroot whose glibc is Bootlin stable-2020.02-2's 2.30:
#   glibc 2.30 headers and libraries (2020.02)  +  kernel UAPI headers (2023.08)
#   +  the SDK's device/library headers and libs  +  gcc 12's libstdc++.a with glibc_compat.o
# libstdc++.so is removed so every C++ link is static, its c++config.h drops the glibc-2.36-only
# mbrtoc8 import, and libgcc_s comes from 2020.02 (gcc 12's wants _dl_find_object, glibc 2.35).
# The executable link needs -Wl,--allow-shlib-undefined because the SDK's libdrm/libz stubs
# reference glibc 2.33/2.34 symbols; at run time the device's own copies are loaded.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mode=${1:?usage: glibc230_toolchain.sh build|verify <sdk> <out|binary>}
sdk=${2:?the 2023.08 SDK}
target=${3:?output toolchain directory, or the binary to verify}
triple=aarch64-buildroot-linux-gnu
old_name=aarch64--glibc--stable-2020.02-2
old_url="https://toolchains.bootlin.com/downloads/releases/toolchains/aarch64/tarballs/$old_name.tar.bz2"
download="${GLIBC230_DOWNLOADS:-$(dirname -- "$sdk")}"

build() {
    out=$target
    [ -e "$out" ] && { echo "$out exists; remove it to rebuild the hybrid toolchain"; exit 0; }
    old=$download/$old_name
    if [ ! -d "$old" ]; then
        echo "== fetching $old_name"
        mkdir -p "$download"
        curl -fL -o "$download/$old_name.tar.bz2" "$old_url"
        tar -C "$download" -xjf "$download/$old_name.tar.bz2"
    fi
    work=$out.partial
    rm -rf "$work"
    echo "== copying the SDK"
    cp -a "$sdk" "$work"
    S=$work/$triple/sysroot OS=$old/$triple/sysroot NS=$sdk/$triple/sysroot
    echo "== glibc 2.30 sysroot"
    rm -rf "$S"
    cp -a "$OS" "$S"
    # Newer kernel UAPI headers, as in the plain build.
    for d in linux asm asm-generic drm misc mtd rdma sound video xen; do
        rm -rf "$S/usr/include/$d"
        cp -a "$NS/usr/include/$d" "$S/usr/include/$d"
    done
    # Device and library headers/libs that are not part of glibc.
    for h in alsa EGL GLES3 KHR gbm.h libdrm libudev.h xf86drm.h xf86drmMode.h zconf.h zlib.h; do
        cp -a "$NS/usr/include/$h" "$S/usr/include/"
    done
    rm -f "$S"/usr/lib/libstdc++.so.6.0.25 "$S"/usr/lib/libstdc++.so.6.0.25-gdb.py
    for l in "$NS"/usr/lib/libasound.* "$NS"/usr/lib/libdrm.* "$NS"/usr/lib/libEGL.* "$NS"/usr/lib/libgbm.* \
             "$NS"/usr/lib/libGLESv2.* "$NS"/usr/lib/libmali.* "$NS"/usr/lib/libmali_hook.* "$NS"/usr/lib/libudev.* \
             "$NS"/usr/lib/libz.* "$NS"/usr/lib/libstdc++.* "$NS"/usr/lib/libgfortran.* "$NS"/usr/lib/libgomp.*; do
        [ -e "$l" ] && cp -a "$l" "$S/usr/lib/"
    done
    cp -a "$NS/usr/lib/pkgconfig" "$S/usr/lib/"
    for l in "$NS"/lib/libatomic.*; do cp -a "$l" "$S/lib/"; done
    # gcc 8's libgcc_s: only the old GCC_3.0..4.2.0 unwinder symbols are used, and the device's own
    # libgcc_s.so.1 is loaded at run time.
    for d in "$S/lib" "$work/$triple/lib64"; do
        rm -f "$d"/libgcc_s.so "$d"/libgcc_s.so.1
        cp -a "$OS"/lib/libgcc_s.so "$OS"/lib/libgcc_s.so.1 "$d/"
    done
    # libstdc++ 12 was configured against glibc 2.37; <cuchar> would import ::mbrtoc8 (glibc 2.36).
    cfg=$(ls -d "$work/$triple"/include/c++/*/"$triple"/bits/c++config.h)
    sed -i -e 's|^#define _GLIBCXX_USE_UCHAR_C8RTOMB_MBRTOC8_CXX20 1|/* #undef _GLIBCXX_USE_UCHAR_C8RTOMB_MBRTOC8_CXX20 (glibc 2.30) */|' \
           -e 's|^#define _GLIBCXX_USE_UCHAR_C8RTOMB_MBRTOC8_FCHAR8_T 1|/* #undef _GLIBCXX_USE_UCHAR_C8RTOMB_MBRTOC8_FCHAR8_T (glibc 2.30) */|' "$cfg"
    # The shared libstdc++ needs glibc 2.34+: remove it so every C++ link (CMake's checks included)
    # takes libstdc++.a, which gets the compat shim for the symbols glibc 2.30 lacks.
    rm -f "$work/$triple"/lib64/libstdc++.so* "$S"/usr/lib/libstdc++.so*
    echo "== compat shim"
    FLIP_TOOLCHAIN=$work "$here/../platform/flip/cc" -O2 -mcpu=cortex-a35 -c "$here/glibc_compat.c" -o "$work/glibc_compat.o"
    for a in "$work/$triple/lib64/libstdc++.a" "$S/usr/lib/libstdc++.a"; do
        "$sdk/bin/aarch64-linux-ar" rs "$a" "$work/glibc_compat.o"
    done
    mv "$work" "$out"
    echo "glibc 2.30 toolchain: $out"
}

verify() {
    bin=$target
    od=$sdk/bin/aarch64-linux-objdump re=$sdk/bin/aarch64-linux-readelf
    echo "NEEDED:"; "$re" -d "$bin" | sed -n 's/.*Shared library: \[\(.*\)\]/  \1/p'
    max=$("$od" -T "$bin" | grep -o 'GLIBC_2\.[0-9]*' | sort -t. -k2 -n -u | tail -1)
    echo "max glibc version: $max"
    echo "version needs per library:"
    "$re" -V "$bin" | awk '/Version needs section/ {on=1} on && /File:/ {f=$5} on && /Name:/ {print "  " f " " $3}' | sort -u
    [ "${max#GLIBC_2.}" -le 30 ] || { echo "FAIL: $bin needs $max"; exit 1; }
    ! "$re" -d "$bin" | grep -q 'libstdc++' || { echo "FAIL: $bin links libstdc++ dynamically"; exit 1; }
    echo "ok: $bin needs at most GLIBC_2.30 and no libstdc++.so"
}

case $mode in
    build) build ;;
    verify) verify ;;
    *) echo "unknown mode $mode" >&2; exit 2 ;;
esac
