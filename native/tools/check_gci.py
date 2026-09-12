#!/usr/bin/env python3
"""Validate a GameCube memory-card save exported as a Dolphin-style .gci file.

Checks the 64-byte big-endian directory entry and that the payload holds an
integral number of 8 KiB blocks. Exits non-zero on any inconsistency.
"""
import struct
import sys
from pathlib import Path

GC_EPOCH_OFFSET = 946684800  # 2000-01-01 in Unix seconds


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    path = Path(sys.argv[1])
    data = path.read_bytes()
    if len(data) < 0x40:
        print(f'{path}: too short for a directory entry ({len(data)} bytes)')
        return 1
    (game, maker, _reserved, banner_flags, filename, mtime, icon_addr, icon_fmt, anim_speed, permissions,
     copy_counter, first_block, block_count, _reserved2, comment_addr) = struct.unpack(
        '>4s2sBB32sIIHHBbHHHI', data[:0x40])
    payload = len(data) - 0x40
    problems = []
    if payload % 0x2000:
        problems.append(f'payload {payload} is not a multiple of 8192')
    if payload // 0x2000 != block_count:
        problems.append(f'block count {block_count} does not match payload {payload // 0x2000} blocks')
    if not game.isalnum():
        problems.append(f'game code {game!r} is not alphanumeric')
    if first_block < 5:
        problems.append(f'first block {first_block} lies in the system area')
    if icon_addr != 0xFFFFFFFF and icon_addr >= payload:
        problems.append(f'icon address {icon_addr:#x} beyond payload')
    if comment_addr != 0xFFFFFFFF and comment_addr + 64 > payload:
        problems.append(f'comment address {comment_addr:#x} beyond payload')
    name = filename.split(b'\0', 1)[0].decode('ascii', 'replace')
    import datetime
    when = datetime.datetime.fromtimestamp(mtime + GC_EPOCH_OFFSET, datetime.timezone.utc)
    print(f'{path.name}: game={game.decode()} maker={maker.decode()} name={name!r} blocks={block_count} '
          f'first_block={first_block} icon={icon_addr:#x} fmt={icon_fmt:#06x} speed={anim_speed:#06x} '
          f'banner={banner_flags:#04x} comment={comment_addr:#x} perms={permissions:#04x} '
          f'copies={copy_counter} modified={when:%Y-%m-%d %H:%M:%SZ}')
    if comment_addr != 0xFFFFFFFF and comment_addr + 64 <= payload:
        comment = data[0x40 + comment_addr:0x40 + comment_addr + 64]
        print('  comment:', comment[:32].split(b'\0', 1)[0].decode('ascii', 'replace'), '/',
              comment[32:].split(b'\0', 1)[0].decode('ascii', 'replace'))
    for problem in problems:
        print('  PROBLEM:', problem)
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
