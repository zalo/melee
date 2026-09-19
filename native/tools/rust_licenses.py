#!/usr/bin/env python3
"""Collect the license notices of the Rust crates compiled into melee_native.

Aurora's disc reader (nod-ffi) is a Rust staticlib built through Corrosion. The crates that end up in
the binary are the ones Cargo compiled for the AArch64 target; their names come from the target
fingerprint directory, their versions from nod's Cargo.lock and their license files from the Cargo
registry. Host-only build scripts and proc macros live under the host target and are not listed.

Writes into <licenses>:
    rust-crates.txt   every crate with its license expression and license files
    bzip2.txt, zstd.txt, liblzma.txt   notices of the C libraries the -sys crates vendor
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import tomllib

ROOT = Path(__file__).resolve().parents[2]
TARGET = 'aarch64-unknown-linux-gnu'
LICENSE_FILE = re.compile(r'^(LICEN[CS]E|COPYING|UNLICENSE|NOTICE)', re.IGNORECASE)
# crate -> (notice name, files inside the crate that carry the vendored C library's own license)
VENDORED = {
    'bzip2-sys': ('bzip2', ['bzip2-1.0.8/LICENSE']),
    'zstd-sys': ('zstd', ['zstd/LICENSE']),
    'liblzma-sys': ('liblzma', ['xz/COPYING', 'xz/COPYING.0BSD']),
    'libz-sys': ('zlib', ['src/zlib/LICENSE']),
}


def compiled_crates(build):
    fingerprints = sorted(build.glob(f'cargo/*/{TARGET}/release/.fingerprint'))
    if not fingerprints:
        raise RuntimeError(f'No {TARGET} Cargo fingerprints under {build}/cargo')
    return sorted({re.sub(r'-[0-9a-f]{16}$', '', p.name) for d in fingerprints for p in d.iterdir()})


def registry_dirs(cargo_home):
    dirs = sorted((cargo_home / 'registry/src').glob('*'))
    if not dirs:
        raise RuntimeError(f'No Cargo registry sources under {cargo_home}')
    return dirs


def write_notices(build, licenses, cargo_home=None, lock=None):
    build = Path(build)
    licenses = Path(licenses)
    cargo_home = Path(cargo_home or os.environ.get('CARGO_HOME') or ROOT / 'build/flip-tools/cargo')
    lock = Path(lock or build / '_deps/aurora_nod-src/Cargo.lock')
    packages = tomllib.loads(lock.read_text())['package']
    registries = registry_dirs(cargo_home)
    seen = {}
    out = ['Rust crates compiled into melee.aarch64 (Aurora disc reader nod-ffi and its dependencies).',
           'Identical license texts are printed once and referenced afterwards.', '']
    written = ['rust-crates.txt']
    for name in compiled_crates(build):
        for package in [p for p in packages if p['name'] == name]:
            version = package['version']
            if 'source' not in package:
                # Workspace crates of nod itself; its license files sit at the workspace root.
                source, license_expr = lock.parent, 'MIT OR Apache-2.0'
            else:
                source = next((r / f'{name}-{version}' for r in registries if (r / f'{name}-{version}').is_dir()), None)
                if source is None:
                    raise RuntimeError(f'{name} {version} is not in the Cargo registry under {cargo_home}')
                manifest = tomllib.loads((source / 'Cargo.toml').read_text())['package']
                license_expr = manifest.get('license') or f"see {manifest.get('license-file')}"
            out += ['=' * 78, f'{name} {version}', f'License: {license_expr}', '=' * 78]
            files = sorted(p for p in source.iterdir() if p.is_file() and LICENSE_FILE.match(p.name))
            for path in files:
                text = path.read_text(errors='replace')
                digest = hashlib.sha256(text.encode()).hexdigest()
                if digest in seen:
                    out += [f'--- {path.name}: same text as {seen[digest]}', '']
                    continue
                seen[digest] = f'{name} {version} {path.name}'
                out += [f'--- {path.name}', text.rstrip(), '']
            if name in VENDORED:
                notice, vendored = VENDORED[name]
                texts = [(source / f).read_text(errors='replace').rstrip() for f in vendored]
                (licenses / f'{notice}.txt').write_text('\n\n'.join(texts) + '\n')
                written.append(f'{notice}.txt')
    (licenses / 'rust-crates.txt').write_text('\n'.join(out) + '\n')
    return written


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build', type=Path, default=ROOT / 'build/native-flip-a35')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cargo-home', type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for name in write_notices(args.build, args.output, args.cargo_home):
        print(args.output / name)


if __name__ == '__main__':
    main()
