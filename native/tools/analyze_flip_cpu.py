#!/usr/bin/env python3
"""Summarize Flip IP samples using the executable saved for that exact build."""
import argparse
from bisect import bisect_right
from collections import Counter, defaultdict
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('prefix', type=Path, help='Trial path without .ips/.maps suffix')
    parser.add_argument('--binary', required=True, type=Path, help='Matching unstripped executable')
    parser.add_argument('--top', type=int, default=12)
    parser.add_argument('--callers', action='store_true', help='Group by x30 return-address snapshot (not a full unwind)')
    args = parser.parse_args()
    prefix = str(args.prefix)
    mappings = []
    for line in Path(prefix+'.maps').read_text().splitlines():
        fields = line.split(maxsplit=5)
        if len(fields) != 6 or 'x' not in fields[1]:
            continue
        start, end = [int(value, 16) for value in fields[0].split('-')]
        mappings.append((start, end, int(fields[2], 16), fields[5]))
    symbols = []
    output = subprocess.check_output(['nm', '-n', '--defined-only', str(args.binary)], text=True)
    for line in output.splitlines():
        fields = line.split(maxsplit=2)
        if len(fields) == 3 and fields[1] in 'tTwW':
            symbols.append((int(fields[0], 16), fields[2]))
    addresses = [address for address, name in symbols]
    threads = dict(line.split(maxsplit=1) for line in Path(prefix+'.threads').read_text().splitlines())
    samples = defaultdict(Counter)
    def resolve(pc):
        label = 'unmapped'
        for start, end, offset, path in mappings:
            if start <= pc < end:
                relative = pc-start+offset
                if Path(path).name == 'melee_native':
                    index = bisect_right(addresses, relative)-1
                    label = symbols[index][1] if index >= 0 else 'executable:unknown'
                else:
                    # Stripped driver offsets are buckets, not inferred functions.
                    label = f'{Path(path).name}:0x{relative & ~0xfff:x}..+0xfff'
                break
        return label
    for line in Path(prefix+'.ips').read_text().splitlines():
        fields = line.split()
        if len(fields) < 2:
            continue
        tid, pc = fields[0], int(fields[1], 16)
        label = resolve(pc)
        if args.callers and len(fields) >= 7:
            label += ' <- ' + resolve(max(0, int(fields[6], 16)-4))
        samples[tid][label] += 1
    for tid, counts in sorted(samples.items(), key=lambda item: -item[1].total()):
        total = counts.total()
        print(f'{tid} {threads.get(tid, "unknown")}: {total} samples')
        top = counts.most_common(args.top)
        names = subprocess.check_output(['c++filt'], input='\n'.join(name for name, _ in top)+'\n', text=True).splitlines()
        for (name, count), pretty in zip(top, names):
            print(f'  {count:5d} {100*count/total:5.1f}% {pretty[:240]}')


if __name__ == '__main__':
    main()
