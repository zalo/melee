#!/usr/bin/env python3
"""Fetch the AArch64 Wayland/xkbcommon client libraries SDL's Wayland driver is configured against.

SDL is built with SDL_WAYLAND_SHARED, so it only needs the headers, pkg-config data and library
sonames at configure time and dlopens the CFW's libwayland-client/-egl/-cursor and libxkbcommon at
run time; nothing from here is linked or shipped. Layout written to --output:

    root/usr/{include,lib/aarch64-linux-gnu}   extracted Debian bookworm arm64 packages
    pkgconfig/*.pc                             pkg-config files pointing at root/, plus link-free
                                               egl.pc and libffi.pc so the dependency checks pass

build.sh exports FLIP_WAYLAND_ROOT=<output>/root and PKG_CONFIG_PATH=<output>/pkgconfig.
"""
import argparse
import hashlib
import io
from pathlib import Path
import tarfile
import urllib.request

MIRROR = 'https://deb.debian.org/debian/pool/main/'
PACKAGES = {
    'w/wayland/libwayland-client0_1.21.0-1_arm64.deb': 'ae390cc04c2eb1de90b9a6373505b22c730ada5e72daa50c507b7f99c12faf06',
    'w/wayland/libwayland-cursor0_1.21.0-1_arm64.deb': '8c8d1a3942bc3fc9dd3ea7c679c1a314e59cdaaa700883d54d1bb3ec89db256b',
    'w/wayland/libwayland-dev_1.21.0-1_arm64.deb': '932405c58195d299495230792e374ad304adadf3d3ffdc07eda3466b756adc33',
    'w/wayland/libwayland-egl1_1.21.0-1_arm64.deb': 'c08124a4d9af24f058b45c88867a84697882e755ea94965795d152fe26fe57c9',
    'libx/libxkbcommon/libxkbcommon0_1.5.0-1_arm64.deb': 'ec518d8a19796a399ab95e7bc4dfbb6bd2ed8e151f77b222df26208db412d852',
    'libx/libxkbcommon/libxkbcommon-dev_1.5.0-1_arm64.deb': '7d39b34b202fe58e18992b439c79c149ebb1b821b58dfb863ff4898e06420699',
}
PC_MODULES = ['wayland-client', 'wayland-cursor', 'wayland-egl', 'xkbcommon']
STUB_PC = {
    'egl': 'Name: egl\nDescription: device libEGL (linked through the build stubs)\nVersion: 1.5\nLibs: -lEGL\nCflags:\n',
    'libffi': 'Name: libffi\nDescription: runtime dependency of libwayland-client (not linked)\nVersion: 3.4.4\nLibs:\nCflags:\n',
}


def deb_data(deb):
    """Return the data.tar.* member of a .deb (an ar archive)."""
    if not deb.startswith(b'!<arch>\n'):
        raise ValueError('not an ar archive')
    offset = 8
    while offset < len(deb):
        header = deb[offset:offset + 60]
        name = header[:16].decode().strip().rstrip('/')
        size = int(header[48:58].decode().strip())
        body = deb[offset + 60:offset + 60 + size]
        if name.startswith('data.tar'):
            return body
        offset += 60 + size + (size & 1)
    raise ValueError('no data.tar member')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = args.output / 'root'
    pkgconfig = args.output / 'pkgconfig'
    root.mkdir(parents=True, exist_ok=True)
    pkgconfig.mkdir(parents=True, exist_ok=True)
    for path, expected in PACKAGES.items():
        with urllib.request.urlopen(MIRROR + path, timeout=120) as response:
            deb = response.read()
        digest = hashlib.sha256(deb).hexdigest()
        if digest != expected:
            raise SystemExit(f'{path}: sha256 {digest}, expected {expected}')
        with tarfile.open(fileobj=io.BytesIO(deb_data(deb))) as archive:
            archive.extractall(root, filter='tar')
    libdir = root / 'usr/lib/aarch64-linux-gnu/pkgconfig'
    for module in PC_MODULES:
        lines = (libdir / f'{module}.pc').read_text().splitlines()
        lines = [f'prefix={root / "usr"}' if line.startswith('prefix=') else line for line in lines]
        (pkgconfig / f'{module}.pc').write_text('\n'.join(lines) + '\n')
    for module, text in STUB_PC.items():
        (pkgconfig / f'{module}.pc').write_text(text)
    print(args.output)


if __name__ == '__main__':
    main()
