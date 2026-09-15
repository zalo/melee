#!/usr/bin/env python3
"""Exercise launcher CPU-state restoration without touching host sysfs."""
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest

SOURCE = Path(__file__).resolve().parents[1] / 'platform/flip/launch.sh'
# Flip-only renderer switches of the direct-GLES builds. This launcher must not
# export any of them: the Aurora it runs does not read them.
RETIRED = ['MELEE_FLIP_BARRIER_EVERY', 'MELEE_FLIP_BATCH_DRAWS', 'MELEE_FLIP_DIRECT_GLES',
           'MELEE_FLIP_DIRECT_PACKET', 'MELEE_FLIP_DIRECT_CHECKS', 'MELEE_FLIP_PRESENT_THREAD',
           'MELEE_FLIP_DIRTY_UPLOAD', 'MELEE_FLIP_ASYNC_PRESENT', 'MELEE_FLIP_FAST_VALIDATION',
           'MELEE_FLIP_ASYNC_FIFO', 'MELEE_FLIP_VERTEX_INPUT', 'MELEE_FLIP_UNIFORM_TABLE']


class LauncherTests(unittest.TestCase):
    def run_launcher(self, mode, all_cores='1', driver='auto', bundled=False, governors=False, performance='1', overrides=None):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cpu_root = root / 'cpus'
            gpu_root = root / 'devfreq'
            cpu_governor = cpu_root / 'cpufreq/policy0/scaling_governor'
            gpu_governor = gpu_root / 'fde60000.gpu/governor'
            dmc_governor = gpu_root / 'dmc/governor'
            if governors:
                for path, value in [(cpu_governor, 'schedutil'), (gpu_governor, 'simple_ondemand'), (dmc_governor, 'dmc_ondemand')]:
                    path.parent.mkdir(parents=True)
                    path.write_text(value + '\n')
            original = ['1', '0', '0']
            for index, value in enumerate(original, 1):
                cpu = cpu_root / f'cpu{index}'
                cpu.mkdir(parents=True)
                (cpu / 'online').write_text(value + '\n')
            if bundled:
                library = root / 'lib/mali-g29p1/libmali.so.1'
                library.parent.mkdir(parents=True)
                library.touch()
            launcher = root / 'launch.sh'
            launcher.write_text(SOURCE.read_text().replace('/sys/devices/system/cpu/', str(cpu_root) + '/').replace('/sys/class/devfreq/', str(gpu_root) + '/'))
            game = root / 'melee_native'
            game.write_text('''#!/bin/sh
cat "$CPU_ROOT"/cpu*/online > observed
printf '%s\\n' "$LD_LIBRARY_PATH" "$XDG_CACHE_HOME" "$MELEE_FLIP_CACHE_HOME" > renderer
for name in $RETIRED; do eval "printf '%s\\n' \\"$name=\\${$name-unset}\\""; done > retired
printf '%s\\n' "${MELEE_FLIP_RESIDENT_DL-unset}" "${MELEE_FLIP_TEXTURE_ATLAS-unset}" > options
if [ -f "$CPU_GOVERNOR" ]; then cat "$CPU_GOVERNOR" "$GPU_GOVERNOR" "$DMC_GOVERNOR" > observed_governors; fi
printf ready > ready
case "$GAME_MODE" in
  hold) exec sleep 30 ;;
  fail) exit 7 ;;
esac
''')
            game.chmod(0o755)
            env = dict(os.environ, CPU_ROOT=str(cpu_root), CPU_GOVERNOR=str(cpu_governor), GPU_GOVERNOR=str(gpu_governor), DMC_GOVERNOR=str(dmc_governor), GAME_MODE=mode, MELEE_FLIP_ALL_CORES=all_cores, MELEE_FLIP_DRIVER=driver, MELEE_FLIP_PERFORMANCE=performance, RETIRED=' '.join(RETIRED))
            for key in RETIRED + ['MELEE_FLIP_RESIDENT_DL', 'MELEE_FLIP_TEXTURE_ATLAS', 'MELEE_FLIP_CACHE_HOME']:
                env.pop(key, None)
            if overrides:
                env.update(overrides)
            proc = subprocess.Popen(['sh', str(launcher)], env=env)
            try:
                if mode == 'hold':
                    deadline = time.monotonic() + 3
                    while not (root / 'ready').exists() and time.monotonic() < deadline:
                        time.sleep(.01)
                    self.assertTrue((root / 'ready').exists())
                    proc.send_signal(signal.SIGTERM)
                result = proc.wait(timeout=5)
                self.assertEqual(result, {'hold': 143, 'fail': 7}.get(mode, 0))
                renderer = (root / 'renderer').read_text().splitlines()
                newer = driver != 'g13' and bundled
                self.assertEqual(renderer[0].split(':')[0], str(root / ('lib/mali-g29p1' if newer else 'lib')))
                self.assertEqual(renderer[1], str(root / ('data/cache/g29-aurora-prs' if newer else 'data/cache/g13-aurora-prs')))
                self.assertEqual(renderer[2], renderer[1])
                self.assertEqual((root / 'retired').read_text().splitlines(), [f'{name}=unset' for name in RETIRED])
                # Renderer options pass through untouched: the game applies its own defaults.
                self.assertEqual((root / 'options').read_text().splitlines(),
                                 [overrides.get('MELEE_FLIP_RESIDENT_DL', 'unset'), overrides.get('MELEE_FLIP_TEXTURE_ATLAS', 'unset')] if overrides else ['unset', 'unset'])
                if governors:
                    self.assertEqual((root / 'observed_governors').read_text().splitlines(), ['performance'] * 3 if performance == '1' else ['schedutil', 'simple_ondemand', 'dmc_ondemand'])
                    self.assertEqual(cpu_governor.read_text().strip(), 'schedutil')
                    self.assertEqual(gpu_governor.read_text().strip(), 'simple_ondemand')
                    self.assertEqual(dmc_governor.read_text().strip(), 'dmc_ondemand')
                expected = ['1'] * 3 if all_cores == '1' else original
                self.assertEqual((root / 'observed').read_text().split(), expected)
                self.assertEqual([(cpu_root / f'cpu{i}/online').read_text().strip() for i in range(1, 4)], original)
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait()

    def test_normal_exit_restores_only_changed_cores(self):
        self.run_launcher('normal')

    def test_game_failure_restores_cores(self):
        self.run_launcher('fail')

    def test_signal_forwards_and_restores_cores(self):
        self.run_launcher('hold')

    def test_bundled_driver_is_selected_without_renderer_flags(self):
        self.run_launcher('normal', bundled=True)

    def test_installed_driver_can_be_selected_with_new_library_present(self):
        self.run_launcher('normal', driver='g13', bundled=True)

    def test_opt_out_preserves_firmware_selection(self):
        self.run_launcher('normal', '0')

    def test_governors_restored_after_normal_exit(self):
        self.run_launcher('normal', bundled=True, governors=True)

    def test_governors_restored_after_failure(self):
        self.run_launcher('fail', bundled=True, governors=True)

    def test_governors_restored_after_signal(self):
        self.run_launcher('hold', bundled=True, governors=True)

    def test_performance_opt_out_preserves_all_governors(self):
        self.run_launcher('normal', bundled=True, governors=True, performance='0')

    def test_renderer_option_overrides_pass_through(self):
        self.run_launcher('normal', bundled=True, overrides={'MELEE_FLIP_RESIDENT_DL': '0', 'MELEE_FLIP_TEXTURE_ATLAS': '0'})


if __name__ == '__main__':
    unittest.main()
