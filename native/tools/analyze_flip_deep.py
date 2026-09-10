#!/usr/bin/env python3
"""Join sampled CPU scopes with GX draw patterns; inclusive scopes overlap."""
import argparse
from collections import defaultdict
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--last-frames', type=int, default=3)
    args = parser.parse_args()
    rows, patterns = [], {}
    for line in args.log.read_text().splitlines():
        if not line.startswith(('[flip-deep]', '[flip-pattern]')):
            continue
        fields = dict(token.split('=', 1) for token in line.split()[1:] if '=' in token)
        if line.startswith('[flip-deep]'):
            rows.append(fields)
        else:
            patterns[(fields['frame'], fields['tag'])] = fields
    selected = set(sorted({int(row['frame']) for row in rows})[-args.last_frames:])
    rows = [row for row in rows if int(row['frame']) in selected]
    cpu_measured = any(row.get('cpu_measured', '1') == '1' for row in rows)
    cost = 'cpu_us' if cpu_measured else 'wall_us'
    metric = 'CPU' if cpu_measured else 'wall'
    print('Selected sampled frames:', sorted(selected))
    print('Host timings only. Inclusive scopes overlap; self time excludes measured child scopes.')
    if not cpu_measured:
        print('Thread CPU clocks disabled to reduce observer cost; CPU columns are unmeasured.')
    print('Off-CPU wall time can include preemption and waits; it is not a GPU-duration measurement.')
    for lane in ['fifo', 'render']:
        lane_rows = [row for row in rows if row['lane'] == lane]
        count = len({row['frame'] for row in lane_rows})
        if not count:
            continue
        sums = defaultdict(lambda: [0., 0., 0., 0.])
        for row in lane_rows:
            for index, field in enumerate(['cpu_us', 'self_cpu_us', 'wall_us', 'calls']):
                sums[row['zone']][index] += float(row[field])
        print(f'\n{lane}: {count} sampled frames; average per sampled frame')
        for zone, (cpu, own, wall, calls) in sorted(sums.items(), key=lambda item: -item[1][0 if cpu_measured else 2]):
            if cpu_measured:
                print(f'  {zone:24s} CPU {cpu/count/1000:7.3f} ms self {own/count/1000:7.3f} ms wall {wall/count/1000:7.3f} ms calls {calls/count:7.1f}')
            else:
                print(f'  {zone:24s} wall {wall/count/1000:7.3f} ms calls {calls/count:7.1f}')
    attributes = defaultdict(lambda: [0., 0.])
    groups = defaultdict(lambda: [0., 0., 0.])
    for row in rows:
        if row['zone'] == 'vertex_attribute':
            key = int(row['tag'])
            attributes[key][0] += float(row[cost])
            attributes[key][1] += int(row['units'])
        if row['zone'] == 'gl_draw_call':
            p = patterns.get((row['frame'], row['tag']))
            if not p:
                continue
            vertices = int(p['vertices'])
            bucket = '<=16' if vertices <= 16 else '<=128' if vertices <= 128 else '>128'
            key = (p['program'], p['tev'], p['vertex_stride'], bucket, p['program_change'], p['pipeline_change'])
            groups[key][0] += float(row[cost])
            groups[key][1] += float(row['wall_us'])
            groups[key][2] += int(row['calls'])
    print(f'\nVertex formats by total sampled {metric} time (attribute, type, components, address, LE, NBT):')
    for key, (cpu, vertices) in sorted(attributes.items(), key=lambda item: -item[1][0])[:12]:
        fields = (key & 255, key >> 8 & 255, key >> 16 & 255, key >> 24 & 255, key >> 32 & 1, key >> 33 & 1)
        print(f'  {fields}: {cpu/1000:.3f} ms, {int(vertices)} vertices, {cpu/max(1,vertices):.3f} us/vertex')
    print('\nGL draw-call patterns (program, TEV stages, stride, vertices, program change, pipeline change):')
    for key, (cpu, wall, calls) in sorted(groups.items(), key=lambda item: -item[1][0])[:15]:
        print(f'  {key}: {cpu/1000:.3f} ms {metric} total, {int(calls)} calls, {cpu/calls:.3f} us {metric}/call, {wall/calls:.3f} us wall/call')


if __name__ == '__main__':
    main()
