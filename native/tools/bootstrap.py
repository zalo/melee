"""Prepare the pinned source-level graphics/runtime dependency.

Aurora comes from the zalo/aurora-arm fork, branch gles-direct-submission. That
branch is upstream encounter/aurora plus the nine performance PRs, the Miyoo
Flip platform hunks and the direct GLES submission fast path, so no patch is
applied on top of the checkout any more.
"""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
checkout = root / 'build/native-deps/aurora'
repository = 'https://github.com/zalo/aurora-arm.git'
branch = 'gles-direct-submission'
revision = '56f18692ee46d0964784d4d50e7f93d9513e18c0'
if not checkout.exists():
    checkout.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['git', 'clone', '--branch', branch, repository, str(checkout)], check=True)
    subprocess.run(['git', '-C', str(checkout), 'checkout', '--detach', revision], check=True)
actual = subprocess.check_output(['git', '-C', str(checkout), 'rev-parse', 'HEAD'], text=True).strip()
if actual != revision:
    raise SystemExit(f'Aurora checkout is {actual}; expected {revision}. Existing checkout left untouched.')
print(f'Aurora source verified at {revision} ({repository} {branch})')
