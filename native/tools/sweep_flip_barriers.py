#!/usr/bin/env python3
"""Greedily screen barrier removals in an already-running, fixed Flip scene.

Requires /tmp/melee-scanout and a frozen/paused real-game scene. Results are
scene-specific candidates, not a universal rendering policy. Kernel markers
avoid the firmware's offset between dmesg timestamps and /proc/uptime.
"""
import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess
import time
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', default='adb')
    parser.add_argument('--device', default='10.0.0.178:5555')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--first', type=int, default=1)
    parser.add_argument('--last', type=int, default=64)
    parser.add_argument('--seconds', type=float, default=3)
    parser.add_argument('--initial-mask', type=lambda s: int(s, 0), default=(1 << 64) - 1)
    args = parser.parse_args()
    if not 1 <= args.first <= args.last <= 64 or args.seconds < 1 or not 0 <= args.initial_mask < 1 << 64:
        parser.error('Require draws 1..64, seconds >= 1, and a uint64 mask')
    args.output.mkdir(parents=True, exist_ok=False)
    adb = [args.adb, '-s', args.device]
    full = (1 << 64) - 1
    results = []
    kept = args.initial_mask

    def shell(command):
        return subprocess.check_output(adb + ['shell', command], text=True).strip()

    def setting(mask):
        value = shlex.quote(f'{mask:#018x} 0 1\n')
        shell(f'printf %s {value} > /tmp/melee-flip-barriers.conf.new && '
              'mv /tmp/melee-flip-barriers.conf.new /tmp/melee-flip-barriers.conf')

    def marker():
        token = 'melee-barriers-' + uuid.uuid4().hex
        shell('echo ' + shlex.quote('<6>' + token) + ' > /dev/kmsg')
        return token

    def faults(token):
        log = shell('dmesg')
        if token not in log:
            raise RuntimeError('Kernel marker lost; cannot attribute faults')
        return [line for line in log.split(token, 1)[1].splitlines()
                if re.search(r'GPU fault|error detected from slot|gpu.*timeout|Out of memory', line, re.I)]

    def capture():
        return shell('/tmp/melee-scanout && sha256sum /tmp/melee-scanout.ppm').split()[0]

    def save():
        (args.output / 'results.json').write_text(json.dumps(dict(
            retained_mask=hex(kept), reference=reference, pid=pid,
            removed=[i for i in range(1, 65) if not kept & (1 << (i - 1))],
            trials=results), indent=2) + '\n')

    pid = shell('pidof melee_native')
    if not pid:
        parser.error('Launch the fixed-scene game before starting the sweep')
    reference = None
    try:
        setting(full)
        time.sleep(1)
        token = marker()
        reference = capture()
        time.sleep(3)
        if capture() != reference or faults(token):
            raise RuntimeError('All-barrier scene is not stable and fault-free')
        setting(kept)
        time.sleep(1)
        token = marker()
        time.sleep(5)
        if capture() != reference or faults(token):
            raise RuntimeError('Initial retained mask failed; start from full barriers')
        def recover_retained():
            nonlocal kept
            # Faulting jobs can poison later short windows. Drain with a full
            # control, then revalidate; revoke recent removals if necessary.
            while True:
                setting(full)
                time.sleep(1)
                recovery = marker()
                time.sleep(2)
                if capture() != reference or faults(recovery):
                    raise RuntimeError('Full-barrier control failed recovery')
                token = marker()
                setting(kept)
                time.sleep(5)
                errors = faults(token)
                if capture() == reference and not errors:
                    return
                removed = [i for i in range(1, 65) if not kept & (1 << (i - 1))]
                if not removed:
                    raise RuntimeError('No removals left to revoke')
                revoked = removed[-1]
                kept |= 1 << (revoked - 1)
                results.append(dict(revoked=revoked, reason='combined revalidation failed',
                                    kernel_marker=token, fault_lines=errors))
                save()
                print('Revoked removal', revoked, flush=True)

        for draw in range(args.first, args.last + 1):
            if shell('pidof melee_native') != pid:
                raise RuntimeError('Game exited or restarted; retain checkpoint and recover')
            candidate = kept & ~(1 << (draw - 1))
            token = marker()
            setting(candidate)
            time.sleep(args.seconds)
            actual = capture()
            errors = faults(token)
            accepted = actual == reference and not errors
            if accepted:
                kept = candidate
            else:
                setting(kept)
                time.sleep(1)
                recovery = marker()
                time.sleep(2)
                if capture() != reference or faults(recovery):
                    recover_retained()
            results.append(dict(draw=draw, mask=hex(candidate), accepted=accepted,
                                capture_hash=actual, kernel_marker=token, fault_lines=errors))
            save()
            print(draw, 'candidate removal' if accepted else 'keep barrier', 'fault records', len(errors), flush=True)
        token = marker()
        time.sleep(15)
        errors = faults(token)
        validated = capture() == reference and not errors
        (args.output / 'validation.json').write_text(json.dumps(dict(
            mask=hex(kept), seconds=15, kernel_marker=token,
            passed=validated, fault_lines=errors), indent=2) + '\n')
        if not validated:
            recover_retained()
            token = marker()
            time.sleep(15)
            errors = faults(token)
            validated = capture() == reference and not errors
            save()
            (args.output / 'validation.json').write_text(json.dumps(dict(
                mask=hex(kept), seconds=15, kernel_marker=token,
                passed=validated, fault_lines=errors), indent=2) + '\n')
        print('Combined validation:', validated, hex(kept), flush=True)
    finally:
        setting(full)
        time.sleep(1)
        shell('rm -f /tmp/melee-flip-barriers.conf')
        (args.output / 'dmesg').write_text(shell('dmesg'))
        subprocess.run(adb + ['pull', '/mnt/SDCARD/Ports/melee-native/data/state/game.log',
                             str(args.output / 'game.log')], check=True)


if __name__ == '__main__':
    main()
