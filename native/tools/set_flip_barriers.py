#!/usr/bin/env python3
"""Atomically change the running Flip renderer's diagnostic barrier selection."""
import argparse
import shlex
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--adb', default='adb')
    p.add_argument('--device', default='10.0.0.178:5555')
    p.add_argument('--mask', type=lambda s: int(s, 0), default=(1 << 64) - 1)
    p.add_argument('--base', type=int, default=0, help='Zero-based start of the 64-draw window')
    p.add_argument('--every', type=int, default=1, help='Keep every Nth barrier; 0 removes all frame-draw barriers')
    p.add_argument('--reset', action='store_true', help='Remove file and restore startup settings')
    p.add_argument('--path', default='/tmp/melee-flip-barriers.conf')
    args = p.parse_args()
    if not 0 <= args.mask < 1 << 64 or not 0 <= args.base < 1 << 32 or not 0 <= args.every < 1 << 32:
        p.error('mask must fit uint64; base/every must fit uint32')
    path = shlex.quote(args.path)
    temporary = shlex.quote(args.path + '.new')
    if args.reset:
        command = f'rm -f {path}'
    else:
        text = f'{args.mask:#018x} {args.base} {args.every}\n'
        command = f"printf %s {shlex.quote(text)} > {temporary} && mv {temporary} {path}"
    subprocess.run([args.adb, '-s', args.device, 'shell', command], check=True)
    print('Configuration written; renderer logs acknowledgement at a frame boundary (poll interval 250 ms).')


if __name__ == '__main__':
    main()
