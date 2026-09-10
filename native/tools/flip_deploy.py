#!/usr/bin/env python3
"""Deploy over passwordless ADB; game data is an explicit, one-time operation."""
import argparse
import hashlib
from pathlib import Path
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', default='adb')
    parser.add_argument('--device', default='10.0.0.178:5555')
    parser.add_argument('--root', default='/mnt/SDCARD/Ports/melee-native')
    commands = parser.add_subparsers(dest='command', required=True)
    disc = commands.add_parser('disc', help='Verify and install an image once; refuse to replace an existing image')
    disc.add_argument('image', type=Path)
    disc.add_argument('--nodtool', required=True)
    build = commands.add_parser('build', help='Upload only runtime files; never uploads disc images')
    build.add_argument('directory', type=Path)
    args = parser.parse_args()
    adb = [args.adb, '-s', args.device]
    subprocess.run([args.adb, 'connect', args.device], check=True)

    def shell(command):
        return subprocess.check_output(adb + ['shell', command], text=True).strip()

    def digest(path):
        with path.open('rb') as stream:
            return hashlib.file_digest(stream, 'sha256').hexdigest()

    def remote_digest(path):
        q = shlex.quote(path)
        return shell(f'if test -f {q}; then sha256sum {q}; fi').split(' ')[0]

    def upload(source, target, immutable=False):
        expected = digest(source)
        existing = remote_digest(target)
        if existing == expected:
            print(f'Unchanged: {target}', flush=True)
            return
        if existing and immutable:
            raise RuntimeError(f'Refusing to replace existing disc: {target}')
        parent = str(Path(target).parent)
        partial = target + '.partial'
        shell('mkdir -p ' + shlex.quote(parent))
        print(f'Uploading {source.name} ({source.stat().st_size:,} bytes)', flush=True)
        subprocess.run(adb + ['push', str(source), partial], check=True)
        if remote_digest(partial) != expected:
            raise RuntimeError(f'Transfer checksum failed: {partial}')
        shell(f'mv {shlex.quote(partial)} {shlex.quote(target)}')
        print(f'Verified: {target}', flush=True)

    root = args.root.rstrip('/')
    if args.command == 'disc':
        if args.image.suffix.lower() not in {'.rvz', '.iso', '.gcm', '.ciso'}:
            raise RuntimeError('Expected a supported GameCube image')
        subprocess.run([args.nodtool, 'verify', str(args.image)], check=True)
        # One stable name lets the launcher and all tests share this copy.
        upload(args.image, root + '/data/disc.img', immutable=True)
    else:
        allowed = {'melee_native', 'melee_flip_gpu_probe', 'launch.sh', 'README.txt',
                   'lib/libstdc++.so.6', 'lib/libgcc_s.so.1', 'lib/mali-g29p1/libmali.so.1'}
        files = sorted(p for p in args.directory.rglob('*') if p.is_file())
        if not (args.directory / 'melee_native').is_file():
            raise RuntimeError('Bundle has no melee_native executable')
        for path in files:
            relative = path.relative_to(args.directory).as_posix()
            if relative not in allowed and not (relative.startswith('licenses/') and path.suffix == '.txt'):
                raise RuntimeError(f'File is not in the runtime allowlist: {relative}')
        for path in files:
            relative = path.relative_to(args.directory).as_posix()
            upload(path, root + '/' + relative)
            if relative in {'melee_native', 'melee_flip_gpu_probe', 'launch.sh'}:
                shell('chmod +x ' + shlex.quote(root + '/' + relative))


if __name__ == '__main__':
    main()
