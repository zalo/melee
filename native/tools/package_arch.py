#!/usr/bin/env python3
"""Create an asset-free pacman package from the checked relocatable bundle.

For an Arch-native source build use native/packaging/PKGBUILD and makepkg.
This conversion also works on the Ubuntu validation/CI host.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('bundle', type=Path)
parser.add_argument('destination', type=Path)
args = parser.parse_args()
bundle = args.bundle.resolve(strict=True)
if not (bundle / 'melee_native').is_file():
    raise SystemExit('Not a Linux runtime bundle')
# Only accept the exact allowlisted output of package_linux.py. In particular,
# a user placing their disc beside the executable must not put it in a package.
manifest = json.loads((bundle.parent / 'manifest.json').read_text())
actual = {}
for file in bundle.rglob('*'):
    if file.is_symlink():
        raise SystemExit(f'Unexpected bundle symlink: {file}')
    if file.is_file():
        actual[str(file.relative_to(bundle))] = hashlib.sha256(file.read_bytes()).hexdigest()
if actual != manifest:
    raise SystemExit('Bundle differs from the generated allowlist manifest; regenerate it before packaging')
args.destination.mkdir(parents=True, exist_ok=True)
archive = args.destination.resolve() / 'melee-native-0.1.0-1-x86_64.pkg.tar.zst'
if archive.exists():
    raise SystemExit(f'Output exists: {archive}')
with tempfile.TemporaryDirectory(prefix='melee-arch-') as tmp:
    stage = Path(tmp)
    shutil.copytree(bundle, stage / 'opt/melee-native')
    (stage / 'usr/bin').mkdir(parents=True)
    (stage / 'usr/bin/melee-native').symlink_to('/opt/melee-native/melee-native')
    (stage / 'usr/share/applications').mkdir(parents=True)
    shutil.copy2(bundle / 'melee-native.desktop', stage / 'usr/share/applications')
    shutil.copytree(bundle / 'Third Party Notices', stage / 'usr/share/licenses/melee-native')
    size = sum(p.stat().st_size for p in stage.rglob('*') if p.is_file() and not p.is_symlink())
    (stage / '.PKGINFO').write_text(f'''pkgname = melee-native
pkgbase = melee-native
pkgver = 0.1.0-1
pkgdesc = Experimental native Melee port using Aurora Vulkan and SDL3
url = https://github.com/jonrosner/melee-native
builddate = {int(os.environ.get('SOURCE_DATE_EPOCH', time.time()))}
packager = Melee Native local build
size = {size}
arch = x86_64
license = custom
depend = glibc>=2.39
depend = libgcc
depend = libstdc++
depend = vulkan-icd-loader
depend = libx11
depend = libxext
depend = libxcursor
depend = libxi
depend = libxrandr
depend = libxfixes
depend = libxss
depend = wayland
depend = libxkbcommon
depend = libdecor
depend = alsa-lib
depend = libpulse
depend = systemd-libs
depend = dbus
optdepend = xdg-desktop-portal-gtk: file chooser portal
''')
    subprocess.run(['tar', '--zstd', '--owner=0', '--group=0', '-cf', str(archive),
                    '-C', str(stage), '.PKGINFO', 'opt', 'usr'], check=True)
listing = subprocess.check_output(['tar', '--zstd', '-tf', str(archive)], text=True)
assert '.PKGINFO\n' in listing and 'opt/melee-native/melee_native\n' in listing
print(archive)
