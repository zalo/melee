"""Prepare the pinned source-level graphics/runtime dependency."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
checkout = root / 'build/native-deps/aurora'
revision = '749d6ee7a22bdfab78c8ece9047bca5d79aa72ca'
if not checkout.exists():
    checkout.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['git', 'clone', 'https://github.com/encounter/aurora.git', str(checkout)], check=True)
    subprocess.run(['git', '-C', str(checkout), 'checkout', '--detach', revision], check=True)
actual = subprocess.check_output(['git', '-C', str(checkout), 'rev-parse', 'HEAD'], text=True).strip()
if actual != revision:
    raise SystemExit(f'Aurora checkout is {actual}; expected {revision}. Existing checkout left untouched.')
print(f'Aurora source verified at {revision}')
