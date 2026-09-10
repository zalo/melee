#!/usr/bin/env python3
"""Reject barrier trials with wrong images, incomplete probes, or new GPU faults."""
import argparse
import json
from pathlib import Path
import re


def inspect_trial(root, name):
    log = (root / (name + '.log')).read_text(errors='replace')
    start = float((root / (name + '.start')).read_text())
    hashes = dict(re.findall(r'\[flip-probe\] phase=(\d+) image hash=([0-9a-f]+)', log))
    faults = []
    for line in (root / (name + '.dmesg')).read_text(errors='replace').splitlines():
        stamp = re.match(r'\[\s*([0-9.]+)\]', line)
        if stamp and float(stamp[1]) >= start and 'DATA_INVALID_FAULT' in line:
            faults.append(line)
    return {'name': name, 'probe_passed': 'PASS: Flip renderer probe' in log and len(hashes) == 7,
            'hashes': hashes, 'new_gpu_faults': len(faults)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--reference', required=True, help='Name of all-barriers control trial in this directory')
    args = parser.parse_args()
    control = inspect_trial(args.directory, args.reference)
    if not control['probe_passed'] or control['new_gpu_faults']:
        parser.error('Control trial failed: warm up/recover the GPU and repeat before comparing subsets')
    results = []
    for path in sorted(args.directory.glob('*.start')):
        result = inspect_trial(args.directory, path.stem)
        result['matches_control'] = result['hashes'] == control['hashes']
        result['accepted'] = result['probe_passed'] and result['matches_control'] and not result['new_gpu_faults']
        results.append(result)
    print(json.dumps(results, indent=2))


if __name__ == '__main__':
    main()
