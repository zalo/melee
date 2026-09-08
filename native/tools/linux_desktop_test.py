#!/usr/bin/env python3
"""X11 integration: portal picker cancel/error/retry and real SDL keyboard events.

Requires an active desktop, xdotool, and a private selected game image.
No screenshots, video, or extracted assets are saved. PAD match coverage uses
run_input_test.rb and run_matrix.rb separately.
"""
import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import subprocess
import time

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('disc', type=Path)
parser.add_argument('--app', type=Path, default=root / 'build/native-linux/melee_native')
args = parser.parse_args()
args.disc = args.disc.resolve(strict=True)
args.app = args.app.resolve(strict=True)
directory = root / 'build/native-desktop' / datetime.now().strftime('%Y%m%d-%H%M%S')
directory.mkdir(parents=True)
report = {'binary': str(args.app), 'checks': []}
print(f'Desktop test logs: {directory}', flush=True)


def xdo(*args):
    return subprocess.run(['xdotool', *map(str, args)], text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, check=True, timeout=10).stdout.strip()


def windows(pattern):
    result = subprocess.run(['xdotool', 'search', '--onlyvisible', '--name', pattern],
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    found = []
    for window in result.stdout.split():
        try:
            pid = xdo('getwindowpid', window)
            if (Path('/proc') / pid / 'comm').read_text().strip().startswith('mutter-x11-fram'):
                continue
        except (OSError, subprocess.CalledProcessError):
            continue
        found.append(window)
    return found


def wait_for(test, description, timeout=45):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        result = test()
        if result:
            return result
        time.sleep(0.1)
    raise RuntimeError(f'Timed out: {description}')


def focus(window):
    xdo('windowactivate', window)
    wait_for(lambda: xdo('getactivewindow') == str(window), 'window focus', 10)
    time.sleep(0.2)


def key(window, name, duration=0.12):
    focus(window)
    xdo('keydown', name)
    time.sleep(duration)
    xdo('keyup', name)


def choose(path):
    window = wait_for(lambda: windows('^Open File$'), 'file chooser')[0]
    focus(window)
    time.sleep(0.4)
    xdo('key', 'ctrl+l')
    time.sleep(0.2)
    xdo('key', 'ctrl+a')
    xdo('type', '--clearmodifiers', '--delay', '3', str(path))
    time.sleep(0.5)
    xdo('key', 'Return')
    time.sleep(1)
    if window in windows('^Open File$'):
        key(window, 'Return')


def start(name):
    state = directory / name
    env = os.environ.copy()
    if env.get('MELEE_TEST_CLEAN_LIBRARY_ENV') == '1':
        env.pop('LD_LIBRARY_PATH', None)
        env.pop('LD_PRELOAD', None)
    env.update(XDG_STATE_HOME=str(state / 'state'), XDG_CONFIG_HOME=str(state / 'config'),
               MELEE_TRACE_INPUT='1', MELEE_AUDIO_CHECK='1')
    log = state / 'state/melee-native/game.log'
    process = subprocess.Popen([str(args.app)], env=env, stdout=subprocess.DEVNULL)
    return process, log


if windows('^Open File$'):
    raise SystemExit('An existing file chooser is open; close it before this test')
process = None
try:
    process, log = start('cancel')
    picker = wait_for(lambda: windows('^Open File$'), 'cancel chooser')[0]
    key(picker, 'Escape')
    assert process.wait(timeout=20) == 0
    report['checks'].append('picker cancel exits 0')
    process, log = start('retry-keyboard')
    invalid = directory / 'invalid.ciso'
    invalid.write_text('Synthetic invalid image for error handling.\n')
    choose(invalid)
    error = wait_for(lambda: windows('could not load disc'), 'invalid image error')[0]
    key(error, 'Return')
    report['checks'].append('invalid image presents error')
    choose(args.disc)

    def scene(number):
        wait_for(lambda: log.exists() and f'[input] ready scene {number}\n' in log.read_text(), f'scene {number}')

    scene(42)
    game = wait_for(lambda: windows('^Melee Native$'), 'game window')[0]
    time.sleep(3)
    key(game, 'd')
    time.sleep(0.4)
    key(game, 'x')
    time.sleep(1)
    key(game, 'x')
    scene(28)
    time.sleep(0.6)
    key(game, 'Return')
    scene(0)
    time.sleep(1.8)
    key(game, 'Return')
    scene(1)
    time.sleep(2.2)
    key(game, 's', 0.04)
    time.sleep(0.4)
    key(game, 'x')
    time.sleep(1.8)
    key(game, 'x')
    scene(8)
    text = log.read_text()
    assert '[launch] Loaded GALE01 revision 2' in text and '[keyboard] down' in text
    assert '[audio] driver=' in text and 'nonzero_samples=' in text
    report['checks'].append('retry selects user image and boots')
    report['checks'].append('X11 keyboard reaches movie/title/menu/CSS through SDL')
    report['checks'].append('audio device opened and samples measured')
    report['status'] = 'pass'
except Exception as error:
    report['status'] = 'failed'
    report['error'] = str(error)
    raise
finally:
    if process and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
    (directory / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2), flush=True)
