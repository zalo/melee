#!/usr/bin/env python3
"""Build a Flip runtime bundle from an explicit file allowlist, without a ROM."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def check_elf(path):
    with path.open('rb') as f:
        header = f.read(20)
    if header[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', header, 18)[0] != 183:
        raise RuntimeError(f'Expected an ELF64 little-endian AArch64 file: {path}')
    versions = subprocess.check_output(['readelf', '--version-info', str(path)], text=True)
    required = [tuple(map(int, s.split('.'))) for s in re.findall(r'Name: GLIBC_([0-9.]+)', versions)]
    if required and max(required) > (2, 36):
        raise RuntimeError(f'{path} requires a newer glibc than the tested Flip firmware')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build/native-flip')
    parser.add_argument('--sdk', type=Path, default=os.environ.get('FLIP_TOOLCHAIN'))
    parser.add_argument('--mali-g29', type=Path, help='Include the verified g29p1 GLES library')
    parser.add_argument('--output', type=Path, default=ROOT / 'dist/flip/Melee-Native-Flip')
    args = parser.parse_args()
    if not args.sdk:
        parser.error('Set FLIP_TOOLCHAIN or pass --sdk')
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output / 'lib').mkdir()
    (args.output / 'licenses').mkdir()
    for name in ['melee_native', 'melee_flip_gpu_probe']:
        source = args.build / name
        if name == 'melee_flip_gpu_probe' and not source.exists():
            continue
        check_elf(source)
        subprocess.run([str(args.sdk / 'bin/aarch64-linux-strip'), '-o', str(args.output / name), str(source)], check=True)
    cpp = args.sdk / 'aarch64-buildroot-linux-gnu/sysroot/usr/lib/libstdc++.so.6'
    check_elf(cpp)
    shutil.copy2(cpp, args.output / 'lib/libstdc++.so.6')
    if args.mali_g29:
        check_elf(args.mali_g29)
        expected = '02d42b4a49007d8b99865653105783034df9adbd502ccef035038a0c3756c027'
        if hashlib.sha256(args.mali_g29.read_bytes()).hexdigest() != expected:
            raise RuntimeError('g29 library does not match the tested driver')
        directory = args.output / 'lib/mali-g29p1'
        directory.mkdir()
        shutil.copy2(args.mali_g29, directory / 'libmali.so.1')
        shutil.copy2(ROOT / 'native/platform/flip/MALI-EULA.txt', args.output / 'licenses/Mali.txt')
        shutil.copy2(ROOT / 'native/platform/flip/MALI-SOURCE.txt', args.output / 'licenses/Mali-source.txt')

    shutil.copy2(ROOT / 'native/platform/flip/launch.sh', args.output / 'launch.sh')
    shutil.copy2(ROOT / 'native/platform/flip/PACKAGE_README.txt', args.output / 'README.txt')
    notices = {
        'Aurora': ROOT / 'build/native-deps/aurora/LICENSE',
        'Dawn': ROOT / 'native/licenses/Dawn.txt',
        'nod': ROOT / 'native/licenses/nod.txt',
        'FreeType': ROOT / 'native/licenses/FreeType.txt',
        'libpng': args.build / '_deps/png-src/LICENSE',
        'fmt': args.build / '_deps/fmt-src/LICENSE',
        'GCC-runtime': ROOT / 'native/platform/flip/GCC-RUNTIME-NOTICE.txt',
        'GCC-exception': ROOT / 'native/platform/flip/GCC-RUNTIME-EXCEPTION.txt',
        'GCC-GPL3': ROOT / 'native/platform/flip/GCC-GPL3.txt',
    }
    for line in (args.build / 'package-licenses.txt').read_text().splitlines():
        name, source = line.split('\t')
        notices[Path(name).stem] = Path(source)
    for name, source in notices.items():
        shutil.copy2(source, args.output / 'licenses' / (name + '.txt'))
    print(args.output)


if __name__ == '__main__':
    main()
