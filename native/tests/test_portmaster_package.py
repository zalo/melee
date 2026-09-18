#!/usr/bin/env python3
"""Validate the PortMaster port files, the launcher, and (when built) the zip."""
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
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
        self.assertNotIn('GBM', attr['desc'])
        self.assertIn('melee/assets', attr['inst'])
        for extension in ['.iso', '.gcm', '.ciso', '.rvz']:
            self.assertIn(extension, attr['inst'])
        self.assertEqual(attr['genres'], ['action'])
        self.assertIsNone(attr['desc_md'])
        self.assertIsNone(attr['inst_md'])
        self.assertEqual(attr['arch'], ['aarch64'])
        self.assertEqual(attr['availability'], 'paid')
        # Built against a glibc 2.30 sysroot (gettid, pthread_cond_clockwait); the static libstdc++ carries arc4random.
        self.assertEqual(attr['min_glibc'], '2.30')
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
        # PortMaster resolves the image from the ports folder, so it carries the game folder prefix.
        self.assertEqual(game.findtext('image'), './melee/screenshot.jpg')
        self.assertTrue((PORT_DIR / 'screenshot.jpg').exists())
        self.assertIsNone(game.find('players'))
        self.assertTrue(game.findtext('desc'))

    def test_screenshot_is_640x480_jpeg(self):
        data = (PORT_DIR / 'screenshot.jpg').read_bytes()
        self.assertEqual(data[:3], b'\xff\xd8\xff')
        index = 2
        while index < len(data):
            marker = data[index + 1]
            length = struct.unpack('>H', data[index + 2:index + 4])[0]
            if marker in (0xc0, 0xc1, 0xc2):
                height, width = struct.unpack('>HH', data[index + 5:index + 9])
                self.assertEqual((width, height), (640, 480))
                return
            index += 2 + length
        self.fail('no SOF marker')

    def test_readme_mentions_upstreams_and_controls(self):
        text = (PORT_DIR / 'README.md').read_text()
        for needle in ['doldecomp/melee', 'encounter/aurora', 'encounter/dawn', 'PortMaster',
                       'melee/assets', 'Start', 'Select', 'build_portmaster.sh']:
            self.assertIn(needle, text)

    def test_port_files_use_lf(self):
        for name in ['Melee.sh', 'port.json', 'README.md', 'gameinfo.xml']:
            self.assertNotIn(b'\r\n', (PORT_DIR / name).read_bytes(), name)


class LauncherTextTests(unittest.TestCase):
    def setUp(self):
        self.text = (PORT_DIR / 'Melee.sh').read_text()

    def test_syntax(self):
        subprocess.run(['bash', '-n', str(PORT_DIR / 'Melee.sh')], check=True)

    def test_required_portmaster_calls(self):
        for needle in ['source $controlfolder/control.txt', 'mod_${CFW_NAME}.txt', 'get_controls',
                       'GAMEDIR="/$directory/ports/melee"', 'tee "$GAMEDIR/log.txt"', '$GPTOKEYB2 "melee.aarch64" -c "$GAMEDIR/melee.ini"',
                       'pm_platform_helper "$GAMEDIR/melee.aarch64"', 'pm_finish',
                       'chmod +x "$GAMEDIR/melee.aarch64"',
                       'XDG_CONFIG_HOME="$GAMEDIR/runtime/config"', 'XDG_CACHE_HOME', 'SDL_GAMECONTROLLERCONFIG']:
            self.assertIn(needle, self.text, needle)
        for extension in ['*.iso', '*.gcm', '*.ciso', '*.rvz']:
            self.assertIn(extension, self.text)
        # PortMaster review rules: no traps, no killing gptokeyb, no bundled drivers, no sysfs tuning.
        for needle in ['trap ', 'pkill', 'killall', 'mali', 'fde60000.gpu', 'governor', 'devfreq', 'sudo ']:
            self.assertNotIn(needle, self.text, needle)

    def test_launcher_not_executable_in_source(self):
        self.assertFalse(os.stat(PORT_DIR / 'Melee.sh').st_mode & 0o111)


class LauncherBehaviourTests(unittest.TestCase):
    """Run Melee.sh against a fake PortMaster control folder and sysfs."""

    def run_launcher(self, mode='normal', disc=True):
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
GPTOKEYB="{root}/gptokeyb-classic"
GPTOKEYB2="{root}/gptokeyb"
get_controls() {{ sdl_controllerconfig="fake-map"; }}
pm_message() {{ printf '%s\\n' "$1" > "{root}/message"; }}
pm_platform_helper() {{ printf '%s\\n' "$1" > "{root}/helper"; }}
pm_finish() {{ printf finished > "{root}/finished"; }}
''')
        (control / 'mod_testcfw.txt').write_text(f'printf sourced > "{root}/mod"\n')
        (root / 'gptokeyb').write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$GPTOKEYB_LOG"\nexit 0\n')
        (root / 'gptokeyb').chmod(0o755)
        if disc:
            (gamedir / 'assets').mkdir()
            (gamedir / 'assets/Melee (USA) (v1.02).RVZ').write_bytes(b'not a disc')
        text = (PORT_DIR / 'Melee.sh').read_text()
        text = text.replace('controlfolder="/roms/ports/PortMaster"', f'controlfolder="{control}"')
        launcher = root / 'Melee.sh'
        launcher.write_text(text)
        game = gamedir / 'melee.aarch64'
        # Zip extraction drops the exec bit; the launcher must restore it.
        game.write_text(f'''#!/bin/sh
printf '%s\\n' "$1" > "{root}/disc"
printf '%s\\n' "$LD_LIBRARY_PATH" "$XDG_CONFIG_HOME" "$XDG_STATE_HOME" "$XDG_CACHE_HOME" "$SDL_GAMECONTROLLERCONFIG" > "{root}/env"
[ "$GAME_MODE" = fail ] && exit 7
exit 0
''')
        game.chmod(0o644)
        env = dict(os.environ, GAME_MODE=mode, GPTOKEYB_LOG=str(root / 'gptokeyb.log'), HOME=str(root))
        result = subprocess.run(['bash', str(launcher)], env=env, stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL, timeout=30).returncode
        return root, gamedir, result

    def test_normal_run(self):
        root, gamedir, result = self.run_launcher()
        self.assertEqual(result, 0)
        self.assertTrue((root / 'mod').exists())
        self.assertEqual((root / 'disc').read_text().strip(), str(gamedir / 'assets/Melee (USA) (v1.02).RVZ'))
        env = (root / 'env').read_text().splitlines()
        # Nothing is bundled (static C++ runtime), so the launcher leaves the library path alone.
        self.assertNotIn('libs.aarch64', env[0])
        self.assertEqual(env[1:4], [str(gamedir / 'runtime/config'), str(gamedir / 'runtime/state'),
                                    str(gamedir / 'runtime/cache')])
        self.assertEqual(env[4], 'fake-map')
        self.assertEqual((root / 'gptokeyb.log').read_text().split(),
                         ['melee.aarch64', '-c', str(gamedir / 'melee.ini')])
        self.assertEqual((root / 'helper').read_text().strip(), str(gamedir / 'melee.aarch64'))
        self.assertTrue((root / 'finished').exists())
        self.assertTrue((gamedir / 'log.txt').exists())

    def test_missing_disc_reports_and_exits(self):
        root, gamedir, result = self.run_launcher(disc=False)
        self.assertEqual(result, 1)
        self.assertIn('melee/assets', (root / 'message').read_text())
        self.assertFalse((root / 'disc').exists())

    def test_game_failure_still_finishes(self):
        root, gamedir, result = self.run_launcher(mode='fail')
        self.assertTrue((root / 'disc').exists())
        self.assertTrue((root / 'finished').exists())


class AssembleTests(unittest.TestCase):
    def test_layout_and_zip_from_fake_bundle(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bundle = root / 'bundle'
            (bundle / 'lib').mkdir(parents=True)
            (bundle / 'licenses').mkdir()
            (bundle / 'melee_native').write_bytes(b'\x7fELF\x02\x01\x01' + bytes(9) + struct.pack('<HH', 3, 183) + bytes(44))
            (bundle / 'lib/libstdc++.so.6').write_bytes(b'cpp')
            (bundle / 'licenses/Aurora.txt').write_text('license')
            (bundle / 'licenses/Mali.txt').write_text('driver eula')
            (bundle / 'launch.sh').write_text('flip only')
            tree, zip_path = package.assemble(bundle, root / 'out')
            self.assertEqual(tree, root / 'out/melee')
            for name in ['port.json', 'Melee.sh', 'README.md', 'gameinfo.xml', 'screenshot.jpg',
                         'melee/melee.aarch64', 'melee/melee.ini',
                         'melee/licenses/LICENSE.Aurora.txt', 'melee/assets/README.txt']:
                self.assertTrue((tree / name).exists(), name)
            self.assertFalse((tree / 'melee/libs.aarch64').exists())
            self.assertEqual([p.name for p in (tree / 'melee/licenses').iterdir()], ['LICENSE.Aurora.txt'])
            self.assertTrue((tree / 'melee/runtime').is_dir())
            self.assertFalse((tree / 'melee/launch.sh').exists())
            self.assertFalse((tree / 'melee/lib').exists())
            self.assertFalse(os.access(tree / 'Melee.sh', os.X_OK))
            self.assertTrue(os.access(tree / 'melee/melee.aarch64', os.X_OK))
            with zipfile.ZipFile(zip_path) as archive:
                names = set(archive.namelist())
                for name in ['Melee.sh', 'port.json', 'melee/melee.aarch64', 'melee/melee.ini',
                             'melee/licenses/LICENSE.Aurora.txt', 'melee/assets/README.txt', 'melee/runtime/']:
                    self.assertIn(name, names, name)
                self.assertFalse([n for n in names if 'libstdc++' in n or 'libs.aarch64' in n])
                self.assertFalse([n for n in names if not (n.startswith('melee/') or n in ('Melee.sh', 'port.json'))])
                self.assertEqual((archive.getinfo('melee/melee.aarch64').external_attr >> 16) & 0o777, 0o755)
                self.assertTrue(is_aarch64_elf(archive.read('melee/melee.aarch64')[:20]))
            with self.assertRaises(FileExistsError):
                package.assemble(bundle, root / 'out')


class RustLicenseTests(unittest.TestCase):
    def test_notices_from_fake_build_and_registry(self):
        spec = importlib.util.spec_from_file_location('rust_licenses', NATIVE / 'tools/rust_licenses.py')
        rust_licenses = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(rust_licenses)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build = root / 'build'
            target = build / 'cargo/nod-ffi_1/aarch64-unknown-linux-gnu/release/.fingerprint'
            target.mkdir(parents=True)
            for name in ['nod-0123456789abcdef', 'bytes-0123456789abcdef', 'bzip2-sys-fedcba9876543210']:
                (target / name).mkdir()
            # Host-only build tools must not be listed.
            (build / 'cargo/nod-ffi_1/release/.fingerprint/cc-0123456789abcdef').mkdir(parents=True)
            nod = build / '_deps/aurora_nod-src'
            nod.mkdir(parents=True)
            (nod / 'LICENSE-MIT').write_text('nod mit')
            (nod / 'Cargo.lock').write_text('''
[[package]]
name = "nod"
version = "2.0.0"

[[package]]
name = "bytes"
version = "1.0.0"
source = "registry+https://github.com/rust-lang/crates.io-index"

[[package]]
name = "bzip2-sys"
version = "0.1.13+1.0.8"
source = "registry+https://github.com/rust-lang/crates.io-index"

[[package]]
name = "cc"
version = "1.0.0"
source = "registry+https://github.com/rust-lang/crates.io-index"
''')
            registry = root / 'cargo/registry/src/index.crates.io-1'
            for crate, license_expr in [('bytes-1.0.0', 'MIT'), ('bzip2-sys-0.1.13+1.0.8', 'MIT/Apache-2.0')]:
                (registry / crate).mkdir(parents=True)
                (registry / crate / 'Cargo.toml').write_text(f'[package]\nname = "x"\nlicense = "{license_expr}"\n')
                (registry / crate / 'LICENSE-MIT').write_text('same mit text')
            (registry / 'bzip2-sys-0.1.13+1.0.8/bzip2-1.0.8').mkdir()
            (registry / 'bzip2-sys-0.1.13+1.0.8/bzip2-1.0.8/LICENSE').write_text('Julian Seward')
            licenses = root / 'licenses'
            licenses.mkdir()
            written = rust_licenses.write_notices(build, licenses, cargo_home=root / 'cargo')
            self.assertEqual(sorted(written), ['bzip2.txt', 'rust-crates.txt'])
            text = (licenses / 'rust-crates.txt').read_text()
            for needle in ['bytes 1.0.0', 'License: MIT', 'bzip2-sys 0.1.13+1.0.8', 'nod 2.0.0', 'nod mit',
                           'same text as bytes 1.0.0 LICENSE-MIT']:
                self.assertIn(needle, text)
            self.assertNotIn('cc 1.0.0', text)
            self.assertEqual(text.count('same mit text'), 1)
            self.assertIn('Julian Seward', (licenses / 'bzip2.txt').read_text())
            (registry / 'bytes-1.0.0/Cargo.toml').unlink()
            (registry / 'bytes-1.0.0/LICENSE-MIT').unlink()
            (registry / 'bytes-1.0.0').rmdir()
            with self.assertRaises(RuntimeError):
                rust_licenses.write_notices(build, licenses, cargo_home=root / 'cargo')


@unittest.skipUnless(ZIP.exists(), 'no built melee.zip (set MELEE_PORTMASTER_ZIP)')
class BuiltZipTests(unittest.TestCase):
    def test_zip_layout_and_binary(self):
        with zipfile.ZipFile(ZIP) as archive:
            names = set(archive.namelist())
            for name in ['Melee.sh', 'port.json', 'melee/melee.aarch64',
                         'melee/assets/README.txt', 'melee/runtime/']:
                self.assertIn(name, names, name)
            licenses = [n for n in names if n.startswith('melee/licenses/') and not n.endswith('/')]
            self.assertTrue(licenses)
            for name in licenses:
                self.assertRegex(name, r'^melee/licenses/LICENSE\.[^/]+\.txt$')
            self.assertFalse([n for n in names if n.lower().endswith(('.iso', '.gcm', '.ciso', '.rvz', '.img'))])
            self.assertFalse([n for n in names if 'mali' in n.lower() or 'libegl' in n.lower() or 'libgles' in n.lower()])
            self.assertFalse([n for n in names if not (n.startswith('melee/') or n in ('Melee.sh', 'port.json'))])
            self.assertTrue(is_aarch64_elf(archive.read('melee/melee.aarch64')[:20]))
            self.assertFalse([n for n in names if 'libstdc++' in n or 'libs.aarch64' in n])
            self.assertEqual(json.loads(archive.read('port.json')), json.loads((PORT_DIR / 'port.json').read_text()))
            self.assertEqual(archive.read('Melee.sh'), (PORT_DIR / 'Melee.sh').read_bytes())
        tree = ZIP.parent / 'melee'
        if tree.exists():
            for name in ['port.json', 'README.md', 'gameinfo.xml', 'screenshot.jpg', 'Melee.sh', 'melee/melee.aarch64']:
                self.assertTrue((tree / name).exists(), name)


if __name__ == '__main__':
    unittest.main()
