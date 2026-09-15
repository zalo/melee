#!/usr/bin/env python3
"""Validate the PortMaster port files, the launcher, and (when built) the zip."""
import importlib.util
import json
import os
from pathlib import Path
import signal
import struct
import subprocess
import tempfile
import time
import unittest
import xml.etree.ElementTree as ElementTree
import zipfile

NATIVE = Path(__file__).resolve().parents[1]
PORT_DIR = NATIVE / 'platform/portmaster'
DIST = NATIVE.parent / 'dist/portmaster'
ZIP = Path(os.environ.get('MELEE_PORTMASTER_ZIP', DIST / 'melee.zip'))

spec = importlib.util.spec_from_file_location('package_portmaster', NATIVE / 'tools/package_portmaster.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


def is_aarch64_elf(header):
    return header[:6] == b'\x7fELF\x02\x01' and struct.unpack_from('<H', header, 18)[0] == 183


class PortJsonTests(unittest.TestCase):
    def setUp(self):
        self.port = json.loads((PORT_DIR / 'port.json').read_text())

    def test_required_fields(self):
        self.assertEqual(self.port['version'], 4)
        self.assertEqual(self.port['name'], 'melee.zip')
        self.assertEqual(self.port['items'], ['Melee.sh', 'melee'])
        self.assertEqual(self.port['items_opt'], [])
        attr = self.port['attr']
        self.assertEqual(attr['title'], 'Super Smash Bros. Melee (native)')
        self.assertEqual(attr['porter'], ['sh1ftmaker'])
        self.assertIn('Aurora', attr['desc'])
        self.assertIn('GLES', attr['desc'])
        self.assertIn('melee/assets', attr['inst'])
        for extension in ['.iso', '.gcm', '.ciso', '.rvz']:
            self.assertIn(extension, attr['inst'])
        self.assertEqual(attr['genres'], ['fighting', 'action'])
        self.assertEqual(attr['arch'], ['aarch64'])
        self.assertEqual(attr['availability'], 'paid')
        self.assertEqual(attr['min_glibc'], '2.36')
        self.assertIs(attr['rtr'], False)
        self.assertEqual(attr['runtime'], [])
        for key in ['desc_md', 'inst_md', 'image', 'exp', 'store', 'reqs']:
            self.assertIn(key, attr)

    def test_gameinfo(self):
        game = ElementTree.parse(PORT_DIR / 'gameinfo.xml').getroot().find('game')
        self.assertEqual(game.findtext('path'), './Melee.sh')
        self.assertEqual(game.findtext('name'), 'Super Smash Bros. Melee (native)')
        self.assertEqual(game.findtext('developer'), 'HAL Laboratory')
        self.assertEqual(game.findtext('publisher'), 'Nintendo')
        self.assertTrue(game.findtext('releasedate').startswith('20011121'))
        self.assertEqual(game.findtext('image'), './melee.png')
        self.assertTrue(game.findtext('desc'))

    def test_screenshot_is_640x480_png(self):
        header = (PORT_DIR / 'screenshot.png').read_bytes()[:24]
        self.assertEqual(header[:8], b'\x89PNG\r\n\x1a\n')
        width, height = struct.unpack('>II', header[16:24])
        self.assertEqual((width, height), (640, 480))

    def test_readme_mentions_upstreams_and_controls(self):
        text = (PORT_DIR / 'README.md').read_text()
        for needle in ['doldecomp/melee', 'encounter/aurora', 'encounter/dawn', 'PortMaster',
                       'melee/assets', 'MELEE_PM_SWAP_CONTROLS', 'Start', 'Select', 'build_portmaster.sh',
                       'RK3566', 'untested']:
            self.assertIn(needle, text)


class LauncherTextTests(unittest.TestCase):
    def setUp(self):
        self.text = (PORT_DIR / 'Melee.sh').read_text()

    def test_syntax(self):
        subprocess.run(['bash', '-n', str(PORT_DIR / 'Melee.sh')], check=True)

    def test_required_portmaster_calls(self):
        for needle in ['source $controlfolder/control.txt', 'mod_${CFW_NAME}.txt', 'get_controls',
                       'GAMEDIR="/$directory/ports/melee"', 'tee "$GAMEDIR/log.txt"', '$GPTOKEYB "melee.aarch64"',
                       'pm_platform_helper "$GAMEDIR/melee.aarch64"', 'pm_finish', './melee.aarch64 "$disc"',
                       'libs.${DEVICE_ARCH}', '$ESUDO', 'MELEE_FLIP_PRESENT_THREAD', 'MELEE_FLIP_ASYNC_PRESENT',
                       'MELEE_PM_DRIVER', '/proc/device-tree/compatible', 'rk3566', 'MELEE_PM_SWAP_CONTROLS',
                       'scaling_governor', '/sys/class/devfreq/*/governor', 'cpu[0-9]*/online',
                       'trap restore_system EXIT', 'XDG_CONFIG_HOME="$GAMEDIR/runtime/config"',
                       'XDG_CACHE_HOME', '-nt "$CACHE_DIR/.binary-stamp"']:
            self.assertIn(needle, self.text, needle)
        for extension in ['*.iso', '*.gcm', '*.ciso', '*.rvz']:
            self.assertIn(extension, self.text)
        # No Flip-only fixed sysfs paths.
        self.assertNotIn('fde60000.gpu', self.text)
        self.assertNotIn('policy0/scaling_governor', self.text)


class LauncherBehaviourTests(unittest.TestCase):
    """Run Melee.sh against a fake PortMaster control folder, sysfs and device tree."""

    def run_launcher(self, mode='normal', compatible='rockchip,rk3566-evb1-ddr4-v10 rockchip,rk3566',
                     bundled=True, disc=True, env_overrides=None):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        gamedir = root / 'ports/melee'
        gamedir.mkdir(parents=True)
        control = root / 'PortMaster'
        control.mkdir()
        (control / 'control.txt').write_text(f'''
ESUDO=""
directory="{str(root).lstrip('/')}"
DEVICE_ARCH=aarch64
CFW_NAME=testcfw
GPTOKEYB="{root}/gptokeyb"
get_controls() {{ sdl_controllerconfig="fake-map"; }}
pm_message() {{ printf '%s\\n' "$1" > "{root}/message"; }}
pm_platform_helper() {{ printf '%s\\n' "$1" > "{root}/helper"; }}
pm_finish() {{ printf finished > "{root}/finished"; }}
''')
        (control / 'mod_testcfw.txt').write_text(f'printf sourced > "{root}/mod"\n')
        (root / 'gptokeyb').write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$GPTOKEYB_LOG"\nexit 0\n')
        (root / 'gptokeyb').chmod(0o755)
        sys_root = root / 'sys'
        for name, value in [('cpufreq/policy0/scaling_governor', 'schedutil'), ('cpufreq/policy4/scaling_governor', 'performance')]:
            path = sys_root / 'cpu' / name
            path.parent.mkdir(parents=True)
            path.write_text(value + '\n')
            (path.parent / 'scaling_available_governors').write_text('performance schedutil\n')
        for index, value in enumerate(['1', '0', '0'], 1):
            cpu = sys_root / 'cpu' / f'cpu{index}'
            cpu.mkdir(parents=True)
            (cpu / 'online').write_text(value + '\n')
        for name, value, available in [('fde60000.gpu', 'simple_ondemand', 'performance simple_ondemand'),
                                       ('dmc', 'dmc_ondemand', 'dmc_ondemand performance'),
                                       ('nogov', 'userspace', 'userspace powersave')]:
            path = sys_root / 'devfreq' / name / 'governor'
            path.parent.mkdir(parents=True)
            path.write_text(value + '\n')
            (path.parent / 'available_governors').write_text(available + '\n')
        (root / 'compatible').write_bytes(compatible.replace(' ', '\0').encode() + b'\0')
        if bundled:
            library = gamedir / 'lib/mali-g29p1/libmali.so.1'
            library.parent.mkdir(parents=True)
            library.touch()
        (gamedir / 'libs.aarch64').mkdir()
        if disc:
            (gamedir / 'assets').mkdir()
            (gamedir / 'assets/Melee (USA) (v1.02).RVZ').write_bytes(b'not a disc')
        text = (PORT_DIR / 'Melee.sh').read_text()
        text = text.replace('controlfolder="/roms/ports/PortMaster"', f'controlfolder="{control}"')
        text = text.replace('/sys/devices/system/cpu/', str(sys_root / 'cpu') + '/')
        text = text.replace('/sys/class/devfreq/', str(sys_root / 'devfreq') + '/')
        text = text.replace('/proc/device-tree/compatible', str(root / 'compatible'))
        launcher = root / 'Melee.sh'
        launcher.write_text(text)
        game = gamedir / 'melee.aarch64'
        game.write_text(f'''#!/bin/sh
printf '%s\\n' "$1" > "{root}/disc"
printf '%s\\n' "$LD_LIBRARY_PATH" "$XDG_CONFIG_HOME" "$XDG_STATE_HOME" "$XDG_CACHE_HOME" "$MELEE_FLIP_CACHE_HOME" > "{root}/env"
printf '%s\\n' "${{MELEE_FLIP_PRESENT_THREAD-unset}}" "${{MELEE_FLIP_ASYNC_PRESENT-unset}}" "${{MELEE_FLIP_SWAP_CONTROLS-unset}}" "${{MELEE_FLIP_TEXTURE_ATLAS-unset}}" "$SDL_GAMECONTROLLERCONFIG" > "{root}/options"
cat "{sys_root}"/cpu/cpu*/online > "{root}/cores"
cat "{sys_root}"/cpu/cpufreq/policy0/scaling_governor "{sys_root}"/devfreq/fde60000.gpu/governor "{sys_root}"/devfreq/dmc/governor "{sys_root}"/devfreq/nogov/governor > "{root}/governors"
printf ready > "{root}/ready"
case "$GAME_MODE" in
  hold) exec sleep 30 ;;
  fail) exit 7 ;;
esac
''')
        game.chmod(0o755)
        # A stale cache from an older binary must be wiped; the marker proves it.
        cache = gamedir / 'runtime/cache/pipeline'
        cache.mkdir(parents=True)
        (cache / 'stale.bin').touch()
        (cache / '.binary-stamp').touch()
        os.utime(cache / '.binary-stamp', (0, 0))
        env = dict(os.environ, GAME_MODE=mode, GPTOKEYB_LOG=str(root / 'gptokeyb.log'), HOME=str(root))
        for key in ['MELEE_FLIP_PRESENT_THREAD', 'MELEE_FLIP_ASYNC_PRESENT', 'MELEE_FLIP_SWAP_CONTROLS',
                    'MELEE_FLIP_TEXTURE_ATLAS', 'MELEE_FLIP_CACHE_HOME', 'MELEE_PM_DRIVER', 'MELEE_PM_DISC',
                    'MELEE_PM_SWAP_CONTROLS', 'MELEE_PM_PERFORMANCE', 'MELEE_PM_ALL_CORES']:
            env.pop(key, None)
        if env_overrides:
            env.update(env_overrides)
        proc = subprocess.Popen(['bash', str(launcher)], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            if mode == 'hold':
                deadline = time.monotonic() + 5
                while not (root / 'ready').exists() and time.monotonic() < deadline:
                    time.sleep(.01)
                self.assertTrue((root / 'ready').exists())
                proc.send_signal(signal.SIGTERM)
            result = proc.wait(timeout=30)
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
        return root, gamedir, result

    def test_normal_run_selects_bundled_driver_on_rk3566(self):
        root, gamedir, result = self.run_launcher()
        self.assertEqual(result, 0)
        self.assertTrue((root / 'mod').exists())
        self.assertEqual((root / 'disc').read_text().strip(), str(gamedir / 'assets/Melee (USA) (v1.02).RVZ'))
        env = (root / 'env').read_text().splitlines()
        self.assertEqual(env[0].split(':')[:2], [str(gamedir / 'lib/mali-g29p1'), str(gamedir / 'libs.aarch64')])
        self.assertEqual(env[1], str(gamedir / 'runtime/config'))
        self.assertEqual(env[2], str(gamedir / 'runtime/state'))
        self.assertEqual(env[3], str(gamedir / 'runtime/cache/pipeline'))
        self.assertEqual(env[4], env[3])
        options = (root / 'options').read_text().splitlines()
        self.assertEqual(options[:4], ['1', '1', 'unset', 'unset'])
        self.assertEqual(options[4], 'fake-map')
        self.assertEqual((root / 'cores').read_text().split(), ['1', '1', '1'])
        self.assertEqual((root / 'governors').read_text().split(), ['performance', 'performance', 'performance', 'userspace'])
        # Restored after exit.
        self.assertEqual([(root / f'sys/cpu/cpu{i}/online').read_text().strip() for i in range(1, 4)], ['1', '0', '0'])
        self.assertEqual((root / 'sys/cpu/cpufreq/policy0/scaling_governor').read_text().strip(), 'schedutil')
        self.assertEqual((root / 'sys/devfreq/fde60000.gpu/governor').read_text().strip(), 'simple_ondemand')
        self.assertEqual((root / 'sys/devfreq/dmc/governor').read_text().strip(), 'dmc_ondemand')
        self.assertEqual((root / 'gptokeyb.log').read_text().strip(), 'melee.aarch64')
        self.assertEqual((root / 'helper').read_text().strip(), str(gamedir / 'melee.aarch64'))
        self.assertTrue((root / 'finished').exists())
        self.assertFalse((gamedir / 'runtime/cache/pipeline/stale.bin').exists())
        self.assertTrue((gamedir / 'runtime/cache/pipeline/.binary-stamp').exists())
        self.assertTrue((gamedir / 'log.txt').exists())

    def test_system_driver_on_other_soc_and_passthrough(self):
        root, gamedir, result = self.run_launcher(compatible='rockchip,rk3326', env_overrides={
            'MELEE_FLIP_TEXTURE_ATLAS': '0', 'MELEE_PM_SWAP_CONTROLS': '0', 'MELEE_FLIP_PRESENT_THREAD': '0'})
        self.assertEqual(result, 0)
        env = (root / 'env').read_text().splitlines()
        self.assertEqual(env[0].split(':')[0], str(gamedir / 'libs.aarch64'))
        self.assertNotIn('mali-g29p1', env[0])
        self.assertEqual((root / 'options').read_text().splitlines()[:4], ['0', '1', '0', '0'])

    def test_driver_override_and_missing_bundle(self):
        root, gamedir, result = self.run_launcher(compatible='rockchip,rk3326', env_overrides={'MELEE_PM_DRIVER': 'bundled'})
        self.assertEqual(result, 0)
        self.assertEqual((root / 'env').read_text().splitlines()[0].split(':')[0], str(gamedir / 'lib/mali-g29p1'))
        root, gamedir, result = self.run_launcher(bundled=False, env_overrides={'MELEE_PM_DRIVER': 'bundled'})
        self.assertEqual(result, 1)
        self.assertFalse((root / 'disc').exists())
        root, gamedir, result = self.run_launcher(env_overrides={'MELEE_PM_DRIVER': 'system'})
        self.assertNotIn('mali-g29p1', (root / 'env').read_text().splitlines()[0])

    def test_missing_disc_reports_and_exits(self):
        root, gamedir, result = self.run_launcher(disc=False)
        self.assertEqual(result, 1)
        self.assertIn('melee/assets', (root / 'message').read_text())
        self.assertFalse((root / 'disc').exists())

    def test_failure_and_signal_restore_system_state(self):
        for mode, expected in [('fail', 7), ('hold', 143)]:
            root, gamedir, result = self.run_launcher(mode=mode)
            self.assertEqual(result, expected, mode)
            self.assertEqual([(root / f'sys/cpu/cpu{i}/online').read_text().strip() for i in range(1, 4)], ['1', '0', '0'])
            self.assertEqual((root / 'sys/cpu/cpufreq/policy0/scaling_governor').read_text().strip(), 'schedutil')
            self.assertEqual((root / 'sys/devfreq/dmc/governor').read_text().strip(), 'dmc_ondemand')

    def test_performance_opt_out(self):
        root, gamedir, result = self.run_launcher(env_overrides={'MELEE_PM_PERFORMANCE': '0', 'MELEE_PM_ALL_CORES': '0'})
        self.assertEqual(result, 0)
        self.assertEqual((root / 'cores').read_text().split(), ['1', '0', '0'])
        self.assertEqual((root / 'governors').read_text().split(), ['schedutil', 'simple_ondemand', 'dmc_ondemand', 'userspace'])


class AssembleTests(unittest.TestCase):
    def test_layout_and_zip_from_fake_bundle(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bundle = root / 'bundle'
            (bundle / 'lib/mali-g29p1').mkdir(parents=True)
            (bundle / 'licenses').mkdir()
            (bundle / 'melee_native').write_bytes(b'\x7fELF\x02\x01\x01' + bytes(9) + struct.pack('<HH', 3, 183) + bytes(44))
            (bundle / 'lib/libstdc++.so.6').write_bytes(b'cpp')
            (bundle / 'lib/mali-g29p1/libmali.so.1').write_bytes(b'mali')
            (bundle / 'licenses/Aurora.txt').write_text('license')
            (bundle / 'launch.sh').write_text('flip only')
            tree, zip_path = package.assemble(bundle, root / 'out')
            self.assertEqual(tree, root / 'out/melee')
            for name in ['port.json', 'Melee.sh', 'README.md', 'gameinfo.xml', 'screenshot.png',
                         'melee/melee.aarch64', 'melee/libs.aarch64/libstdc++.so.6', 'melee/lib/mali-g29p1/libmali.so.1',
                         'melee/licenses/Aurora.txt', 'melee/assets/README.txt']:
                self.assertTrue((tree / name).exists(), name)
            self.assertTrue((tree / 'melee/runtime').is_dir())
            self.assertFalse((tree / 'melee/launch.sh').exists())
            self.assertTrue(os.access(tree / 'Melee.sh', os.X_OK))
            self.assertTrue(os.access(tree / 'melee/melee.aarch64', os.X_OK))
            with zipfile.ZipFile(zip_path) as archive:
                names = set(archive.namelist())
                for name in ['Melee.sh', 'port.json', 'melee/melee.aarch64', 'melee/libs.aarch64/libstdc++.so.6',
                             'melee/licenses/Aurora.txt', 'melee/assets/README.txt', 'melee/runtime/']:
                    self.assertIn(name, names, name)
                self.assertFalse([n for n in names if not (n.startswith('melee/') or n in ('Melee.sh', 'port.json'))])
                self.assertEqual((archive.getinfo('Melee.sh').external_attr >> 16) & 0o777, 0o755)
                self.assertEqual((archive.getinfo('melee/melee.aarch64').external_attr >> 16) & 0o777, 0o755)
                self.assertTrue(is_aarch64_elf(archive.read('melee/melee.aarch64')[:20]))
            with self.assertRaises(FileExistsError):
                package.assemble(bundle, root / 'out')


@unittest.skipUnless(ZIP.exists(), 'no built melee.zip (set MELEE_PORTMASTER_ZIP)')
class BuiltZipTests(unittest.TestCase):
    def test_zip_layout_and_binary(self):
        with zipfile.ZipFile(ZIP) as archive:
            names = set(archive.namelist())
            self.assertIn('Melee.sh', names)
            self.assertIn('port.json', names)
            self.assertIn('melee/melee.aarch64', names)
            self.assertIn('melee/libs.aarch64/libstdc++.so.6', names)
            self.assertIn('melee/assets/README.txt', names)
            self.assertIn('melee/runtime/', names)
            self.assertTrue([n for n in names if n.startswith('melee/licenses/') and n.endswith('.txt')])
            self.assertFalse([n for n in names if n.lower().endswith(('.iso', '.gcm', '.ciso', '.rvz', '.img'))])
            self.assertFalse([n for n in names if not (n.startswith('melee/') or n in ('Melee.sh', 'port.json'))])
            self.assertTrue(is_aarch64_elf(archive.read('melee/melee.aarch64')[:20]))
            self.assertTrue(is_aarch64_elf(archive.read('melee/libs.aarch64/libstdc++.so.6')[:20]))
            self.assertEqual(json.loads(archive.read('port.json')), json.loads((PORT_DIR / 'port.json').read_text()))
            self.assertEqual(archive.read('Melee.sh'), (PORT_DIR / 'Melee.sh').read_bytes())
        tree = ZIP.parent / 'melee'
        if tree.exists():
            for name in ['port.json', 'README.md', 'gameinfo.xml', 'screenshot.png', 'Melee.sh', 'melee/melee.aarch64']:
                self.assertTrue((tree / name).exists(), name)


if __name__ == '__main__':
    unittest.main()
