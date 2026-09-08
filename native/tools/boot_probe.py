"""Developer-only startup debugger. Missing APIs abort; they never report success.

The production melee_mac target still requires a complete static link. This
probe lets us fix real startup crashes while other runtime modules are being
implemented. Generated failure traps remain under the ignored build directory.
"""
import argparse
from pathlib import Path
import re
import shlex
import subprocess
from disc import Disc

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('disc', type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
build = root / 'build/native'
with Disc(a.disc) as disc:
    print(f'Checked {len(disc.files)} disc files', flush=True)
with (root / 'build/native-link.log').open('w') as log:
    result = subprocess.run(['cmake', '--build', str(build), '--target', 'melee_mac', '--', '-j8'], stdout=log, stderr=log)
if result.returncode == 0:
    raise SystemExit('Full game links now. Run and debug melee_mac directly.')
link_log = (root / 'build/native-link.log').read_text()
names = re.findall(r'^  "_([^"\n]+)", referenced from:', link_log, re.M)
if not names or 'error: ' in link_log.replace('clang++: error: linker command failed', ''):
    raise SystemExit('Fix the native build error before running the startup probe')
if not all(re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', n) for n in names):
    raise SystemExit('Unexpected native symbol name')
source = ['#include <stdio.h>', '#include <stdlib.h>']
for name in names:
    source.append('__attribute__((weak,noreturn)) void '+name+'(void) { fputs("UNIMPLEMENTED NATIVE ENTRY: '+name+'\\n", stderr); abort(); }')
(build / 'boot_traps.c').write_text('\n'.join(source)+'\n')
with (root / 'build/native-boot-build.log').open('w') as log:
    subprocess.run(['cmake', '--build', str(build), '--target', 'melee_boot_probe', '--', '-j8'], stdout=log, stderr=log, check=True)
    subprocess.run(['cc', '-c', 'boot_traps.c', '-o', 'boot_traps.o'], cwd=build, stdout=log, stderr=log, check=True)
    commands = subprocess.check_output(['ninja', '-t', 'commands', 'melee_boot_probe'], cwd=build, text=True).splitlines()
    line = next(c for c in commands if ' -o melee_boot_probe.app/Contents/MacOS/melee_boot_probe ' in c)
    args = shlex.split(line)
    if args[:2] != [':', '&&'] or args[-2:] != ['&&', ':']:
        raise SystemExit('Unexpected CMake linker command format')
    subprocess.run(args[2:-2]+['boot_traps.o'], cwd=build, stdout=log, stderr=log, check=True)
print(f'Startup debugger with {len(names)} explicit failure traps; this is not the game release target', flush=True)
with (root / 'build/native-boot-run.log').open('w') as log:
    subprocess.run(['/usr/bin/lldb', '--batch', '-o', 'run', '-o', 'thread backtrace all', '-k', 'thread backtrace all', '--', str(build / 'melee_boot_probe.app/Contents/MacOS/melee_boot_probe'), str(a.disc.resolve())], cwd=root, stdout=log, stderr=log)
print('\n'.join((root / 'build/native-boot-run.log').read_text().splitlines()[-28:]))
