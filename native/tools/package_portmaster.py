#!/usr/bin/env python3
"""Build the PortMaster port of the native Melee port from the AArch64 cross build.

Reuses package_flip.py for the stripped binary and the license set, then lays them out as
PortMaster-New expects:

    <output>/melee/            unzipped tree in the PortMaster-New ports/<name>/ layout
        port.json, Melee.sh, README.md, gameinfo.xml, screenshot.jpg (+ cover.jpg)
        melee/melee.aarch64, melee/melee.ini (gptokeyb2 config),
        melee/licenses/LICENSE.<component>.txt, melee/assets/README.txt, melee/runtime/
    <output>/melee.zip         Melee.sh + melee/ (+ port.json) at the zip root

No disc image, GPU driver or other system library is ever included; the C++ runtime is linked
statically into melee.aarch64.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
PORT_DIR = ROOT / 'native/platform/portmaster'
PORT_NAME = 'melee'
LAUNCHER = 'Melee.sh'
BINARY = 'melee.aarch64'
GPTK_CONFIG = 'melee.ini'
METADATA = ['port.json', 'README.md', 'gameinfo.xml', 'screenshot.jpg']
OPTIONAL_METADATA = ['cover.jpg']
ASSETS_README = '''Put your own Super Smash Bros. Melee disc image in this directory.

Supported: the US 1.02 release (GALE01) as .iso, .gcm, .ciso or .rvz. Dump it
from a disc you own (Dolphin wiki: Ripping Games). The launcher uses the first
image it finds here.

Nothing in this port includes or downloads game data.
'''


def assemble(bundle, output, port_dir=PORT_DIR):
    """Turn a package_flip.py bundle directory into the PortMaster tree and zip.

    Returns (tree, zip_path). `bundle` must contain melee_native and licenses/<component>.txt;
    anything else in it (lib/, launch.sh) is Flip-only and ignored.
    """
    bundle = Path(bundle)
    output = Path(output)
    tree = output / PORT_NAME
    data = tree / PORT_NAME
    if tree.exists():
        raise FileExistsError(f'{tree} already exists; remove it first')
    (data / 'licenses').mkdir(parents=True)
    (data / 'assets').mkdir()
    (data / 'runtime').mkdir()

    shutil.copy2(bundle / 'melee_native', data / BINARY)
    os.chmod(data / BINARY, 0o755)
    for license_file in sorted((bundle / 'licenses').iterdir()):
        if license_file.stem.startswith('Mali'):
            continue
        shutil.copy2(license_file, data / 'licenses' / f'LICENSE.{license_file.stem}.txt')
    (data / 'assets/README.txt').write_text(ASSETS_README)
    shutil.copy2(port_dir / GPTK_CONFIG, data / GPTK_CONFIG)

    # PortMaster convention: the launcher is committed 644; the CFW sets the exec bit.
    shutil.copyfile(port_dir / LAUNCHER, tree / LAUNCHER)
    os.chmod(tree / LAUNCHER, 0o644)
    for name in METADATA:
        shutil.copy2(port_dir / name, tree / name)
    for name in OPTIONAL_METADATA:
        if (port_dir / name).exists():
            shutil.copy2(port_dir / name, tree / name)
    # gameinfo.xml points EmulationStation at ./melee/screenshot.jpg, so the image also ships inside the
    # data folder (the zip carries only Melee.sh, port.json and melee/), as recent ports do.
    shutil.copy2(port_dir / 'screenshot.jpg', data / 'screenshot.jpg')

    zip_path = output / f'{PORT_NAME}.zip'
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as archive:
        add_to_zip(archive, tree / LAUNCHER, LAUNCHER)
        add_to_zip(archive, tree / 'port.json', 'port.json')
        for path in sorted(data.rglob('*')):
            add_to_zip(archive, path, path.relative_to(tree).as_posix())
    return tree, zip_path


def add_to_zip(archive, path, name):
    if path.is_dir():
        info = zipfile.ZipInfo(name + '/')
        info.external_attr = (0o40755 << 16) | 0x10
        archive.writestr(info, b'')
        return
    info = zipfile.ZipInfo.from_file(path, name)
    info.compress_type = zipfile.ZIP_DEFLATED
    mode = 0o755 if os.access(path, os.X_OK) else 0o644
    info.external_attr = (0o100000 | mode) << 16
    with path.open('rb') as f:
        archive.writestr(info, f.read())


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build', type=Path, default=ROOT / 'build/portmaster-a35')
    parser.add_argument('--sdk', type=Path, default=os.environ.get('FLIP_TOOLCHAIN'))
    parser.add_argument('--output', type=Path, default=ROOT / 'dist/portmaster')
    args = parser.parse_args()
    if not args.sdk:
        parser.error('Set FLIP_TOOLCHAIN or pass --sdk')
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as temporary:
        bundle = Path(temporary) / 'bundle'
        command = [sys.executable, str(ROOT / 'native/tools/package_flip.py'), '--build', str(args.build),
                   '--sdk', str(args.sdk), '--output', str(bundle)]
        subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
        tree, zip_path = assemble(bundle, args.output)
    digest = hashlib.sha256(zip_path.read_bytes()).hexdigest()
    print(tree)
    print(f'{zip_path}  sha256={digest}  bytes={zip_path.stat().st_size}')


if __name__ == '__main__':
    main()
