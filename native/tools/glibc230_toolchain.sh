#!/bin/sh
# Build the glibc 2.30 cross toolchain the PortMaster binary is linked with, and verify a binary
# against it.
#
#   glibc230_toolchain.sh build   <sdk> <out>        make the hybrid toolchain at <out> from <sdk>
#   glibc230_toolchain.sh verify  <sdk> <binary> [cxx]
#                                                    fail unless <binary> needs at most GLIBC_2.30 and,
#                                                    of a shared libstdc++, at most GLIBCXX_3.4.28, and
#                                                    exports no C++ runtime symbol; with "cxx" it must
#                                                    link libstdc++.so.6 dynamically
#
# <sdk> is the Bootlin aarch64--glibc--stable-2023.08-1 toolchain that prepare_flip.py filled with the
# device's GLES/GBM/DRM/ALSA libraries. Its glibc is 2.37, so a binary linked against it needs
# GLIBC_2.34+ and cannot start on ArkOS (glibc 2.30) or CrossMix (2.33). port.json promises
# min_glibc 2.30, so the release link uses the same compiler, gcc 12 runtime and prebuilt Dawn but a
# sysroot whose glibc is Bootlin stable-2020.02-2's 2.30:
#   glibc 2.30 headers and libraries (2020.02)  +  kernel UAPI headers (2023.08)
#   +  the SDK's device/library headers and libs  +  gcc 12's libstdc++.a with glibc_compat.o
#   +  libstdcxx-link/libstdc++.so, Debian bullseye's libstdc++ 6.0.28 (GCC 10)
# gcc 12's libstdc++.so is removed so a plain C++ link (CMake's checks) is static, its c++config.h
# drops the glibc-2.36-only mbrtoc8 import, and libgcc_s comes from 2020.02 (gcc 12's wants
# _dl_find_object, glibc 2.35).
# The game links the device's libstdc++.so.6 (PortMaster ports neither bundle nor statically link
# it): -L<out>/libstdcxx-link makes the 6.0.28 library the link target, so the binary asks for no
# symbol version newer than the oldest supported CFW has (ArkOS: glibc 2.30, GLIBCXX_3.4.28), and
# the few gcc 12 symbols that library lacks are taken, hidden, from libstdc++.a (native/CMakeLists.txt).
# The executable link needs -Wl,--allow-shlib-undefined because the SDK's libdrm/libz stubs
# reference glibc 2.33/2.34 symbols; at run time the device's own copies are loaded.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mode=${1:?usage: glibc230_toolchain.sh build|verify <sdk> <out|binary> [cxx]}
sdk=${2:?the 2023.08 SDK}
target=${3:?output toolchain directory, or the binary to verify}
want_cxx=${4:-}
triple=aarch64-buildroot-linux-gnu
old_name=aarch64--glibc--stable-2020.02-2
old_url="https://toolchains.bootlin.com/downloads/releases/toolchains/aarch64/tarballs/$old_name.tar.bz2"
download="${GLIBC230_DOWNLOADS:-$(dirname -- "$sdk")}"
# The shared libstdc++ link target: GCC 10.2's 6.0.28, as Debian bullseye arm64 shipped it. The
# snapshot URL is permanent; the mirror serves the same file while bullseye stays in the pool.
cxx_deb=libstdc++6_10.2.1-6_arm64.deb
cxx_deb_sha256=7869aa540cc46e9f3d4267d5bde2af0e5b429a820c1d6f1a4cfccfe788c31890
cxx_deb_urls="https://snapshot.debian.org/archive/debian/20210814T212851Z/pool/main/g/gcc-10/$cxx_deb
https://deb.debian.org/debian/pool/main/g/gcc-10/$cxx_deb"
cxx_so=usr/lib/aarch64-linux-gnu/libstdc++.so.6.0.28
cxx_so_sha256=20a9317b0b33288ad1f004bfaf768c86168b2404f3f89bfb023046ed4b677cd6
cxx_max=28
# Bump when the hybrid's contents change (headers, libraries, .pc files): an existing toolchain with
# an older stamp is rebuilt instead of being trusted (CI restores it from a cache).
layout_version=3

fetch_cxx_deb() {
    deb=$download/$cxx_deb
    if [ ! -f "$deb" ] || [ "$(sha256sum "$deb" | cut -d' ' -f1)" != "$cxx_deb_sha256" ]; then
        echo "== fetching $cxx_deb"
        mkdir -p "$download"
        for url in $cxx_deb_urls; do
            curl -fL --retry 3 -o "$deb.partial" "$url" || continue
            [ "$(sha256sum "$deb.partial" | cut -d' ' -f1)" = "$cxx_deb_sha256" ] && { mv "$deb.partial" "$deb"; break; }
            echo "checksum mismatch from $url" >&2
        done
        rm -f "$deb.partial"
        [ -f "$deb" ] || { echo "could not fetch $cxx_deb" >&2; exit 1; }
    fi
}

build() {
    out=$target
    if [ -e "$out" ]; then
        if [ "$(cat "$out/.melee-layout" 2>/dev/null)" = "$layout_version" ]; then
            echo "$out exists (layout $layout_version); remove it to rebuild the hybrid toolchain"
            exit 0
        fi
        echo "== $out has an older layout; rebuilding"
        rm -rf "$out"
    fi
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
    # Device and library headers/libs that are not part of glibc (pulse, pipewire-0.3 and spa-0.2 are
    # the PulseAudio/PipeWire client headers prepare_flip.py fetches for SDL's dlopen audio backends).
    for h in alsa EGL GLES3 KHR gbm.h libdrm libudev.h xf86drm.h xf86drmMode.h zconf.h zlib.h \
             pulse pipewire-0.3 spa-0.2; do
        cp -a "$NS/usr/include/$h" "$S/usr/include/"
    done
    # SDL's KMSDRM/PulseAudio/PipeWire configure checks (prepare_flip.py writes these, relative to
    # their own location). Missing ones are written again below, after the copy.
    mkdir -p "$S/usr/lib/pkgconfig"
    for p in libdrm.pc gbm.pc libpulse.pc libpipewire-0.3.pc libspa-0.2.pc; do
        [ -e "$NS/usr/lib/pkgconfig/$p" ] && cp -a "$NS/usr/lib/pkgconfig/$p" "$S/usr/lib/pkgconfig/"
    done
    python3 -c "import sys, pathlib; sys.path.insert(0, '$here'); import prepare_flip; prepare_flip.write_sysroot_pkgconfig(pathlib.Path('$S/usr'))"
    rm -f "$S"/usr/lib/libstdc++.so.6.0.25 "$S"/usr/lib/libstdc++.so.6.0.25-gdb.py
    for l in "$NS"/usr/lib/libasound.* "$NS"/usr/lib/libdrm.* "$NS"/usr/lib/libEGL.* "$NS"/usr/lib/libgbm.* \
             "$NS"/usr/lib/libGLESv2.* "$NS"/usr/lib/libmali.* "$NS"/usr/lib/libmali_hook.* "$NS"/usr/lib/libudev.* \
             "$NS"/usr/lib/libpulse.* "$NS"/usr/lib/libpipewire-0.3.* \
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
    # gcc 12's shared libstdc++ needs glibc 2.34+: remove it so a C++ link that does not ask for
    # libstdcxx-link (CMake's checks included) takes libstdc++.a, which gets the compat shim for the
    # symbols glibc 2.30 lacks.
    rm -f "$work/$triple"/lib64/libstdc++.so* "$S"/usr/lib/libstdc++.so*
    # The 6.0.28 link target lives outside the default search path: only a link that names the
    # directory binds to it, and that link must also add libstdc++.a for what 6.0.28 lacks.
    echo "== libstdc++ 6.0.28 link target"
    fetch_cxx_deb
    mkdir -p "$work/libstdcxx-link"
    "$sdk/bin/aarch64-linux-ar" p "$download/$cxx_deb" data.tar.xz | tar -xJO "./$cxx_so" > "$work/libstdcxx-link/libstdc++.so"
    [ "$(sha256sum "$work/libstdcxx-link/libstdc++.so" | cut -d' ' -f1)" = "$cxx_so_sha256" ] ||
        { echo "unexpected libstdc++.so.6.0.28 in $cxx_deb" >&2; exit 1; }
    echo "== compat shim"
    FLIP_TOOLCHAIN=$work "$here/../platform/flip/cc" -O2 -mcpu=cortex-a35 -c "$here/glibc_compat.c" -o "$work/glibc_compat.o"
    for a in "$work/$triple/lib64/libstdc++.a" "$S/usr/lib/libstdc++.a"; do
        "$sdk/bin/aarch64-linux-ar" rs "$a" "$work/glibc_compat.o"
    done
    echo "$layout_version" > "$work/.melee-layout"
    mv "$work" "$out"
    echo "glibc 2.30 toolchain: $out"
}

verify() {
    bin=$target
    od=$sdk/bin/aarch64-linux-objdump re=$sdk/bin/aarch64-linux-readelf
    needed=$("$re" -d "$bin" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p')
    echo "NEEDED:"; echo "$needed" | sed 's/^/  /'
    max=$("$od" -T "$bin" | grep -o 'GLIBC_2\.[0-9]*' | sort -t. -k2 -n -u | tail -1)
    echo "max glibc version: $max"
    echo "version needs per library:"
    "$re" -V "$bin" | awk '/Version needs section/ {on=1} on && /File:/ {f=$5} on && /Name:/ {print "  " f " " $3}' | sort -u
    [ "${max#GLIBC_2.}" -le 30 ] || { echo "FAIL: $bin needs $max"; exit 1; }
    summary="at most GLIBC_2.30"
    if echo "$needed" | grep -qx 'libstdc++\.so\.6'; then
        # Versions are GLIBCXX_3.4 and GLIBCXX_3.4.<n>; anything else is newer than every CFW's.
        cxx=$("$od" -T "$bin" | grep -o 'GLIBCXX_[0-9.]*' | sort -u | sed 's/^GLIBCXX_//' | sort -t. -k1,1n -k2,2n -k3,3n | tail -1)
        abi=$("$od" -T "$bin" | grep -o 'CXXABI_[0-9.]*' | sort -u | sed 's/^CXXABI_//' | sort -t. -k1,1n -k2,2n -k3,3n | tail -1)
        echo "max libstdc++ versions: GLIBCXX_$cxx CXXABI_$abi"
        case $cxx in
            3.4) ;;
            3.4.*) [ "${cxx#3.4.}" -le "$cxx_max" ] || { echo "FAIL: $bin needs GLIBCXX_$cxx (limit 3.4.$cxx_max)"; exit 1; } ;;
            *) echo "FAIL: $bin needs GLIBCXX_$cxx (limit 3.4.$cxx_max)"; exit 1 ;;
        esac
        summary="$summary, GLIBCXX_$cxx of libstdc++.so.6"
    elif echo "$needed" | grep -q 'libstdc++'; then
        echo "FAIL: $bin links an unexpected libstdc++"; exit 1
    elif [ "$want_cxx" = cxx ]; then
        echo "FAIL: $bin does not link libstdc++.so.6 (PortMaster ports use the device's)"; exit 1
    else
        summary="$summary and no libstdc++.so"
    fi
    # A std:: or C++ runtime symbol in the dynamic symbol table would be bound in place of the
    # device libstdc++'s own by every library loaded after the executable.
    exported=$("$re" --dyn-syms -W "$bin" | awk '$7 != "UND" && $8 ~ /^(_ZN?K?S[taiso]|_ZT[ISV]S[taiso]|_ZT[ISV]?N?K?9__gnu_cxx|_ZN?K?9__gnu_cxx|_ZT[vhc]|_Zn[aw]|_Zd[al]|__cxa_|__gxx_)/ {print $8}')
    if [ -n "$exported" ]; then
        echo "FAIL: $bin exports C++ runtime symbols:"; echo "$exported" | head -20 | sed 's/^/  /'; exit 1
    fi
    echo "ok: $bin needs $summary; no exported C++ runtime symbols"
}

case $mode in
    build) build ;;
    verify) verify ;;
    *) echo "unknown mode $mode" >&2; exit 2 ;;
esac
