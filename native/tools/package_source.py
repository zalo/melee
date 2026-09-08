#!/usr/bin/env python3
"""Archive the committed source tree for the local Arch PKGBUILD."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tarfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('destination', type=Path, nargs='?', default=root / 'dist/arch')
args = parser.parse_args()
args.destination.mkdir(parents=True, exist_ok=True)
archive = args.destination.resolve() / 'melee-native-0.1.0.tar.gz'
if archive.exists():
    raise SystemExit(f'Output already exists: {archive}')
subprocess.run(['git', '-C', str(root), 'archive', '--format=tar.gz',
                '--prefix=melee-native-0.1.0/', '-o', str(archive), 'HEAD'], check=True)
with tarfile.open(archive) as tar:
    for item in tar:
        path = Path(item.name)
        if path.suffix.lower() in ('.iso', '.gcm', '.ciso', '.rvz', '.dol', '.elf', '.ttf', '.otf', '.woff', '.woff2', '.fnt', '.ssm', '.mth', '.usd', '.dat', '.gci', '.so', '.a', '.o', '.dylib', '.dll', '.exe') or '/build/' in item.name or '/dist/' in item.name:
            raise RuntimeError(f'Unexpected generated/private source archive entry: {item.name}')
sha = hashlib.sha256(archive.read_bytes()).hexdigest()
pkgbuild = subprocess.check_output(['git', '-C', str(root), 'show', 'HEAD:native/packaging/PKGBUILD'], text=True)
pkgbuild = pkgbuild.replace("sha256sums=('SKIP') # Local source archive; record its SHA256 before distributing.", f"sha256sums=('{sha}')")
(args.destination / 'PKGBUILD').write_text(pkgbuild)
print(f'{archive}\nSHA256 {sha}')
