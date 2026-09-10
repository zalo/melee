#!/usr/bin/env python3
"""Compare scripted real-game barrier trials with a repeatable control capture."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import statistics


def inspect(root, name):
    def read(suffix):
        path = root / f'{name}.{suffix}'
        return path.read_text(errors='replace') if path.exists() else ''
    start = float(read('start'))
    marker = read('marker').strip()
    kernel = read('dmesg')
    if marker:
        if marker not in kernel:
            raise ValueError('Kernel marker missing from ' + name)
        faults = sum(bool(re.search(r'GPU fault|error detected from slot|gpu.*timeout', line, re.I))
                     for line in kernel.split(marker, 1)[1].splitlines())
    else:
        # Historical files use different clocks: positive counts are not causal attribution.
        faults = sum('DATA_INVALID_FAULT' in line and float(match[1]) >= start
                     for line in kernel.splitlines()
                     if (match := re.match(r'\[\s*([\d.]+)\]', line)))
    log = read('log')
    match_log = log.split('[input-test] ready scene 2\n', 1)
    frames = [float(x) for x in re.findall(r'frame_ms=([\d.]+)', match_log[1])] if len(match_log) == 2 else []
    capture = root / f'{name}.ppm'
    return dict(name=name, new_gpu_faults=faults, attribution="kernel-marker" if marker else "legacy-clock-unverified",
                complete=read('reason').strip() == 'capture-complete' and '[render-detail]' in log,
                capture_sha256=hashlib.sha256(capture.read_bytes()).hexdigest() if capture.exists() else None,
                match_samples=len(frames), mean_match_ms=statistics.mean(frames) if frames else None,
                cores_after=read('cores').strip(), exit_status=read('status').strip())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--reference', default='control')
    args = parser.parse_args()
    control = inspect(args.directory, args.reference)
    if not control['complete'] or control['new_gpu_faults'] or not control['capture_sha256']:
        parser.error('Control incomplete or faulting; recover and repeat before comparing')
    results = []
    for path in sorted(args.directory.glob('*.start')):
        trial = inspect(args.directory, path.stem)
        trial['matches_control'] = trial['capture_sha256'] == control['capture_sha256']
        trial['screen_passed'] = trial['complete'] and trial['matches_control'] and not trial['new_gpu_faults']
        results.append(trial)
    print(json.dumps(results, indent=2))


if __name__ == '__main__':
    main()
