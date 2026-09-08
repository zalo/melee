"""Run the native parser against every archive-shaped asset from a local disc."""
import argparse
from pathlib import Path
import struct
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('test_executable', type=Path)
p.add_argument('assets', type=Path)
a = p.parse_args()
archives = []
for path in sorted(a.assets.rglob('*')):
    if path.suffix in ('.dat', '.usd'):
        with path.open('rb') as file:
            header = file.read(4)
        if len(header) == 4 and struct.unpack('>I', header)[0] == path.stat().st_size:
            archives.append(str(path))
if not archives:
    raise SystemExit('No archives found')
subprocess.run([str(a.test_executable.resolve()), *archives], check=True)
print(f'Validated {len(archives)} archive files')
