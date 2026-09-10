#!/usr/bin/env python3
"""Run a guarded, reproducible backend trial; transfer runtime diagnostics only."""
import argparse
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('name')
    p.add_argument('--driver', choices=['g13', 'g29'], default='g29')
    p.add_argument('--barriers', type=int, choices=[0, 1], default=0)
    p.add_argument('--vertex', type=int, choices=[0, 1], default=1)
    p.add_argument('--table', type=int, choices=[0, 1], default=1)
    p.add_argument('--batch', type=int, choices=[0, 1], default=1)
    p.add_argument('--stage', type=int, default=9)
    p.add_argument('--freeze-after', type=int, default=45, help='0 keeps simulation moving')
    p.add_argument('--capture-frame', type=int, default=120)
    p.add_argument('--env', action='append', default=[], help='Additional MELEE_FLIP_* diagnostic setting, NAME=VALUE')
    p.add_argument('--cpu-samples', action='store_true', help='Collect 12 seconds of user IP samples after first capture; requires --soak >= 15')
    p.add_argument('--cpu-performance', action='store_true', help='Temporarily select the supported CPU performance governor')
    p.add_argument('--gpu-performance', action='store_true', help='Temporarily select the supported GPU performance governor')
    p.add_argument('--dmc-performance', action='store_true', help='Temporarily select the supported memory performance governor')
    p.add_argument('--soak', type=int, default=0, help='Continue this many seconds after the first capture')
    p.add_argument('--output', type=Path, default=ROOT/'native/validation/2026-09-09-flip/staged-backend')
    p.add_argument('--adb', default=str(ROOT/'build/flip-tools/platform-tools/adb'))
    p.add_argument('--device', default='10.0.0.178:5555')
    a = p.parse_args()
    if a.cpu_samples and a.soak < 15: p.error('--cpu-samples requires --soak >= 15')
    if not re.fullmatch('[a-z0-9_-]+', a.name): p.error('Use a unique lowercase trial name')
    if not 0 <= a.freeze_after <= 36000 or not 1 <= a.capture_frame <= 36000 or not 0 <= a.soak <= 120:
        p.error('Invalid diagnostic duration')
    if not 0 <= a.stage <= 328: p.error('Invalid stage number')
    if a.batch and not (a.vertex and a.table): p.error('Batching requires vertex inputs and uniform tables')
    adb = [a.adb, '-s', a.device]
    def shell(s): return subprocess.check_output(adb+['shell', s], text=True).strip()
    if shell('pidof melee_native || true'): raise RuntimeError('A game is already running')
    if not shell('pidof MainUI || true'): raise RuntimeError('MainUI must be available for display handoff')
    if a.cpu_samples:
        subprocess.run(adb+['push',str(ROOT/'build/flip-tools/flip-cpu-sample'),'/tmp/flip-cpu-sample'],check=True,stdout=subprocess.DEVNULL)
    scanout = ROOT/'build/native-flip/melee_flip_scanout'
    if scanout.exists():
        subprocess.run(adb+['push', str(scanout), '/tmp/melee-scanout'], check=True, stdout=subprocess.DEVNULL)
    subprocess.run(adb+['push', str(ROOT/'native/tests/flip-match-controls.input'), '/tmp/melee-match-controls.input'],
                   check=True, stdout=subprocess.DEVNULL)
    # /tmp is RAM-backed on the Flip. Keeping repeated captures and kernel logs
    # there consumed hundreds of MiB and eventually tripped the memory guard.
    remote_root = '/mnt/SDCARD/Ports/melee-native/data/diagnostics/backend-trials'
    remote = remote_root + '/' + a.name
    if (a.output/(a.name+'.start')).exists():
        raise RuntimeError('Local trial evidence exists; choose a new name')
    if shell('test -f '+shlex.quote(remote+'.start')+' && echo exists || true'):
        raise RuntimeError('Trial exists; choose a new name to preserve its evidence')
    script = (ROOT/'native/tools/flip_game_barrier_trial.sh').read_text()
    script = script.replace('root=/tmp/game-barrier-bisect', 'root='+shlex.quote(remote_root))
    exports = [f'export MELEE_FLIP_VERTEX_INPUT={a.vertex} MELEE_FLIP_UNIFORM_TABLE={a.table} MELEE_FLIP_BATCH_DRAWS={a.batch}',
               f'export MELEE_FLIP_BARRIER_EVERY={a.barriers} MELEE_CAPTURE_INTERVAL={a.capture_frame}',
               'unset MELEE_TEST_FREEZE_AFTER',
               f'export MELEE_FLIP_BARRIER_CONFIG=/tmp/melee-backend-unused-config']
    for setting in a.env:
        if not re.fullmatch(r'MELEE_FLIP_[A-Z0-9_]+=[A-Za-z0-9_.-]+', setting): p.error('Invalid diagnostic environment setting')
        exports.append('export '+shlex.quote(setting))
    if a.freeze_after: exports += [f'export MELEE_TEST_FREEZE_AFTER={a.freeze_after}']
    if a.driver == 'g29':
        if shell('test -f /mnt/SDCARD/Ports/melee-native/lib/mali-g29p1/libmali.so.1 && echo yes || true'):
            exports += ['unset LD_PRELOAD', 'export MELEE_FLIP_DRIVER=g29']
        else:
            exports += ['export LD_PRELOAD=/mnt/SDCARD/Ports/melee-native/data/diagnostics/mali-g29p1-candidate/libmali.so.1']
        exports += ['export MELEE_FLIP_CACHE_HOME=/mnt/SDCARD/Ports/melee-native/data/diagnostics/cache-g29-dense']
    else:
        exports += ['unset LD_PRELOAD', 'export MELEE_FLIP_DRIVER=g13']
    script = script.replace('export MELEE_FLIP_PROFILE=1', '\n'.join(exports)+'\nexport MELEE_FLIP_PROFILE=1')
    performance_setup, performance_restore = [], []
    for enabled, name, path in [(a.dmc_performance,'dmc','/sys/class/devfreq/dmc/governor'),
                                (a.gpu_performance,'gpu','/sys/class/devfreq/fde60000.gpu/governor'),
                                (a.cpu_performance,'cpu','/sys/devices/system/cpu/cpufreq/policy0/scaling_governor')]:
        if enabled:
            performance_setup += [f'{name}_path={path}', f'{name}_previous=$(cat "${name}_path")', f'echo performance > "${name}_path"']
            performance_restore += [f'echo "${name}_previous" > "${name}_path"']
    if performance_setup:
        performance_setup.insert(0, "trap '"+'; '.join(performance_restore)+"' EXIT")
        script=script.replace('timeout -s TERM', '\n'.join(performance_setup)+'\ntimeout -s TERM')
    script = script.replace('MELEE_TEST_STAGE=9', f'MELEE_TEST_STAGE={a.stage}')
    if a.cpu_samples:
        script=script.replace('   if /tmp/melee-scanout;', """   if [ ! -f "$root/$name.ips" ]; then
    game_process=$(pidof melee_native)
    cat /proc/$game_process/maps > "$root/$name.maps"
    for task in /proc/$game_process/task/*; do echo "$(basename "$task") $(cat "$task/comm")"; done > "$root/$name.threads"
    /tmp/flip-cpu-sample "$game_process" 12 > "$root/$name.ips" 2> "$root/$name.sample-errors" &
   fi
   if /tmp/melee-scanout;""")
    if a.soak:
        script = script.replace('   if /tmp/melee-scanout;', f'''   now=$(cut -d . -f 1 /proc/uptime)
   if [ ! -f "$root/$name.warm" ]; then echo "$now" > "$root/$name.warm"; fi
   if [ "$((now - $(cat "$root/$name.warm")))" -lt {a.soak} ]; then sleep 2; continue; fi
   if /tmp/melee-scanout;''')
    a.output.mkdir(parents=True, exist_ok=True)
    (a.output/(a.name+'.sh')).write_text(script)
    with tempfile.NamedTemporaryFile(mode='w', suffix='.sh') as f:
        f.write(script); f.flush()
        subprocess.run(adb+['push', f.name, '/tmp/cmd_to_run.sh'], check=True, stdout=subprocess.DEVNULL)
    shell(f'echo {a.name} > /tmp/game-barrier-name; echo 0xffffffffffffffff > /tmp/game-barrier-mask; '
          'echo 0 > /tmp/game-barrier-base; chmod +x /tmp/cmd_to_run.sh; kill -TERM $(pidof MainUI)')
    print('Running '+a.name, flush=True)
    deadline = time.monotonic()+210
    while not shell('test -f '+shlex.quote(remote+'.done')+' && echo done || true'):
        if time.monotonic() > deadline: raise RuntimeError('Device watchdog did not finish; inspect device state')
        time.sleep(2)
    for suffix in ['start','marker','dmesg','reason','status','memory','sensors','cores','log','ppm','scanout.ppm','scanout-error','ips','maps','threads','sample-errors']:
        source = remote+'.'+suffix
        if shell('test -f '+shlex.quote(source)+' && echo yes || true'):
            subprocess.run(adb+['pull',source,str(a.output/(a.name+'.'+suffix))], check=True, stdout=subprocess.DEVNULL)
    reason=a.output/(a.name+'.reason')
    print(a.name+': '+(reason.read_text().strip() if reason.exists() else 'no completion reason'), flush=True)


if __name__ == '__main__': main()
