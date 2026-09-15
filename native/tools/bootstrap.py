"""Prepare the pinned source-level graphics/runtime dependency."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
checkout = root / 'build/native-deps/aurora'
revision = 'd0c931da2ed3f41d0e42736c2ab52a78c7cf1a9d'
if not checkout.exists():
    checkout.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['git', 'clone', 'https://github.com/encounter/aurora.git', str(checkout)], check=True)
    subprocess.run(['git', '-C', str(checkout), 'checkout', '--detach', revision], check=True)
actual = subprocess.check_output(['git', '-C', str(checkout), 'rev-parse', 'HEAD'], text=True).strip()
if actual != revision:
    raise SystemExit(f'Aurora checkout is {actual}; expected {revision}. Existing checkout left untouched.')
print(f'Aurora source verified at {revision}')
