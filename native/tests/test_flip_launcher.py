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


class LauncherTests(unittest.TestCase):
    def run_launcher(self, mode, all_cores='1', driver='auto', bundled=False, barrier_override=None, governors=False, performance='1', pipeline_overrides=None):
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
printf '%s\n' "$MELEE_FLIP_BARRIER_EVERY" "$MELEE_FLIP_VERTEX_INPUT" "$MELEE_FLIP_UNIFORM_TABLE" "$LD_LIBRARY_PATH" "$MELEE_FLIP_BATCH_DRAWS" "$MELEE_FLIP_DIRECT_GLES" > renderer
printf '%s\n' "${MELEE_FLIP_DIRECT_PACKET-unset}" "${MELEE_FLIP_DIRECT_CHECKS-unset}" "${MELEE_FLIP_PRESENT_THREAD-unset}" "${MELEE_FLIP_DIRTY_UPLOAD-unset}" > pipeline
if [ -f "$CPU_GOVERNOR" ]; then cat "$CPU_GOVERNOR" "$GPU_GOVERNOR" "$DMC_GOVERNOR" > observed_governors; fi
printf ready > ready
case "$GAME_MODE" in
  hold) exec sleep 30 ;;
  fail) exit 7 ;;
esac
''')
            game.chmod(0o755)
            env = dict(os.environ, CPU_ROOT=str(cpu_root), CPU_GOVERNOR=str(cpu_governor), GPU_GOVERNOR=str(gpu_governor), DMC_GOVERNOR=str(dmc_governor), GAME_MODE=mode, MELEE_FLIP_ALL_CORES=all_cores, MELEE_FLIP_DRIVER=driver, MELEE_FLIP_PERFORMANCE=performance)
            for key in ["MELEE_FLIP_VERTEX_INPUT", "MELEE_FLIP_UNIFORM_TABLE", "MELEE_FLIP_BARRIER_EVERY", "MELEE_FLIP_BATCH_DRAWS", "MELEE_FLIP_DIRECT_GLES"]:
                env.pop(key, None)
            pipeline_keys = ['MELEE_FLIP_DIRECT_PACKET', 'MELEE_FLIP_DIRECT_CHECKS', 'MELEE_FLIP_PRESENT_THREAD', 'MELEE_FLIP_DIRTY_UPLOAD']
            for key in pipeline_keys:
                env.pop(key, None)
            if pipeline_overrides:
                env.update(zip(pipeline_keys, pipeline_overrides))
            if barrier_override is not None:
                env["MELEE_FLIP_BARRIER_EVERY"] = barrier_override
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
                self.assertEqual(renderer[:3], [barrier_override if barrier_override is not None else ('0' if newer else '1'), '1', '1'])
                self.assertEqual(renderer[3].split(':')[0], str(root / ('lib/mali-g29p1' if newer else 'lib')))
                self.assertEqual(renderer[4], '1' if newer else '0')
                self.assertEqual(renderer[5], '5' if newer and barrier_override in (None, '0') else '0')
                self.assertEqual((root / 'pipeline').read_text().splitlines(),
                                 pipeline_overrides or (['1', '0', '1', '1'] if renderer[5] == '5' else ['unset'] * 4))
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

    def test_bundled_driver_selects_barrier_free_backend(self):
        self.run_launcher('normal', bundled=True)

    def test_installed_driver_can_be_selected_with_new_library_present(self):
        self.run_launcher('normal', driver='g13', bundled=True)

    def test_explicit_barriers_override_new_driver_default(self):
        self.run_launcher('normal', bundled=True, barrier_override='1')

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

    def test_pipeline_overrides_are_preserved(self):
        self.run_launcher('normal', bundled=True, pipeline_overrides=['0', '1', '0', '0'])


if __name__ == '__main__':
    unittest.main()
