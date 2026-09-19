#!/usr/bin/env python3
"""Validate the PortMaster port files, the launcher, and (when built) the zip."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
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
        # The Markdown variants carry the source links and formatting the plain fields must not.
        self.assertIn('https://github.com/doldecomp/melee', attr['desc_md'])
        self.assertIn('https://github.com/zalo/melee', attr['desc_md'])
        self.assertIn('`ports/melee/assets/`', attr['inst_md'])
        self.assertNotIn('http', attr['desc'])
        self.assertNotIn('`', attr['inst'])
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
                       'XDG_CONFIG_HOME="$GAMEDIR/runtime/config"', 'XDG_CACHE_HOME', 'SDL_GAMECONTROLLERCONFIG',
                       'export LD_LIBRARY_PATH="$GAMEDIR/libs.${DEVICE_ARCH}:$LD_LIBRARY_PATH"',
                       'export SDL3SHIM_SDL2_VIDEODRIVER="$SDL_VIDEODRIVER"', 'export SDL3SHIM_SDL2_AUDIODRIVER="$SDL_AUDIODRIVER"',
                       'export SDL_VIDEODRIVER=sdl2 SDL_AUDIODRIVER=sdl2']:
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

    def run_launcher(self, mode='normal', disc=True, cfw_env=()):
        cfw_env = dict(cfw_env)
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
printf '%s\\n' "$LD_LIBRARY_PATH" "$XDG_CONFIG_HOME" "$XDG_STATE_HOME" "$XDG_CACHE_HOME" "$SDL_GAMECONTROLLERCONFIG" "$SDL_VIDEODRIVER" "$SDL_AUDIODRIVER" "${{SDL3SHIM_SDL2_VIDEODRIVER:-unset}}" "${{SDL3SHIM_SDL2_AUDIODRIVER:-unset}}" > "{root}/env"
[ "$GAME_MODE" = fail ] && exit 7
exit 0
''')
        game.chmod(0o644)
        env = dict(os.environ, GAME_MODE=mode, GPTOKEYB_LOG=str(root / 'gptokeyb.log'), HOME=str(root))
        for name in ['SDL_VIDEODRIVER', 'SDL_AUDIODRIVER', 'SDL3SHIM_SDL2_VIDEODRIVER', 'SDL3SHIM_SDL2_AUDIODRIVER']:
            env.pop(name, None)
        env.update(cfw_env)
        result = subprocess.run(['bash', str(launcher)], env=env, stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL, timeout=30).returncode
        return root, gamedir, result

    def test_normal_run(self):
        root, gamedir, result = self.run_launcher()
        self.assertEqual(result, 0)
        self.assertTrue((root / 'mod').exists())
        self.assertEqual((root / 'disc').read_text().strip(), str(gamedir / 'assets/Melee (USA) (v1.02).RVZ'))
        env = (root / 'env').read_text().splitlines()
        # Only the SDL3-over-SDL2 shim is bundled; the launcher puts its folder first on the library path.
        self.assertTrue(env[0].startswith(f'{gamedir}/libs.aarch64:'), env[0])
        # SDL3 is pointed at the shim's driver; with no CFW driver names the inner SDL2 auto-picks.
        self.assertEqual(env[5:9], ['sdl2', 'sdl2', 'unset', 'unset'])
        self.assertEqual(env[1:4], [str(gamedir / 'runtime/config'), str(gamedir / 'runtime/state'),
                                    str(gamedir / 'runtime/cache')])
        self.assertEqual(env[4], 'fake-map')
        self.assertEqual((root / 'gptokeyb.log').read_text().split(),
                         ['melee.aarch64', '-c', str(gamedir / 'melee.ini')])
        self.assertEqual((root / 'helper').read_text().strip(), str(gamedir / 'melee.aarch64'))
        self.assertTrue((root / 'finished').exists())
        self.assertTrue((gamedir / 'log.txt').exists())

    def test_cfw_sdl_driver_names_go_to_the_inner_sdl2(self):
        # ROCKNIX exports SDL_VIDEODRIVER=wayland and SDL_AUDIODRIVER=pulseaudio to ports: SDL2 driver
        # names, which the shim must receive while SDL3 itself uses the shim driver.
        root, gamedir, result = self.run_launcher(cfw_env={'SDL_VIDEODRIVER': 'wayland', 'SDL_AUDIODRIVER': 'pulseaudio'})
        self.assertEqual(result, 0)
        env = (root / 'env').read_text().splitlines()
        self.assertEqual(env[5:9], ['sdl2', 'sdl2', 'wayland', 'pulseaudio'])

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
            shim = root / 'shim'
            (shim / 'lib').mkdir(parents=True)
            (shim / 'lib/libSDL3.so.0.5.0').write_bytes(b'\x7fELF shim')
            (shim / 'lib/libSDL3.so.0').symlink_to('libSDL3.so.0.5.0')
            (shim / 'LICENSE.txt').write_text('zlib license text')
            tree, zip_path = package.assemble(bundle, root / 'out', sdl3=shim)
            self.assertEqual(tree, root / 'out/melee')
            for name in ['port.json', 'Melee.sh', 'README.md', 'gameinfo.xml', 'screenshot.jpg',
                         'melee/melee.aarch64', 'melee/melee.ini',
                         'melee/licenses/LICENSE.Aurora.txt', 'melee/assets/README.txt']:
                self.assertTrue((tree / name).exists(), name)
            self.assertEqual((tree / 'melee/libs.aarch64/libSDL3.so.0').read_bytes(), b'\x7fELF shim')
            self.assertFalse((tree / 'melee/libs.aarch64/libSDL3.so.0').is_symlink())
            self.assertTrue(os.access(tree / 'melee/libs.aarch64/libSDL3.so.0', os.X_OK))
            self.assertEqual(sorted(p.name for p in (tree / 'melee/licenses').iterdir()),
                             ['LICENSE.Aurora.txt', 'LICENSE.SDL3-sdl2-backend.txt'])
            notice = (tree / 'melee/licenses/LICENSE.SDL3-sdl2-backend.txt').read_text()
            self.assertIn('bmdhacks/SDL', notice)
            self.assertTrue(notice.endswith('zlib license text'))
            self.assertTrue((tree / 'melee/runtime').is_dir())
            self.assertFalse((tree / 'melee/launch.sh').exists())
            self.assertFalse((tree / 'melee/lib').exists())
            self.assertFalse(os.access(tree / 'Melee.sh', os.X_OK))
            self.assertTrue(os.access(tree / 'melee/melee.aarch64', os.X_OK))
            with zipfile.ZipFile(zip_path) as archive:
                names = set(archive.namelist())
                for name in ['Melee.sh', 'port.json', 'melee/melee.aarch64', 'melee/melee.ini', 'melee/libs.aarch64/libSDL3.so.0',
                             'melee/licenses/LICENSE.Aurora.txt', 'melee/licenses/LICENSE.SDL3-sdl2-backend.txt',
                             'melee/assets/README.txt', 'melee/runtime/']:
                    self.assertIn(name, names, name)
                self.assertFalse([n for n in names if 'libstdc++' in n])
                self.assertEqual([n for n in names if n.startswith('melee/libs.aarch64/') and not n.endswith('/')],
                                 ['melee/libs.aarch64/libSDL3.so.0'])
                self.assertEqual((archive.getinfo('melee/libs.aarch64/libSDL3.so.0').external_attr >> 16) & 0o777, 0o755)
                self.assertFalse([n for n in names if not (n.startswith('melee/') or n in ('Melee.sh', 'port.json'))])
                self.assertEqual((archive.getinfo('melee/melee.aarch64').external_attr >> 16) & 0o777, 0o755)
                self.assertTrue(is_aarch64_elf(archive.read('melee/melee.aarch64')[:20]))
            with self.assertRaises(FileExistsError):
                package.assemble(bundle, root / 'out')
            # Without a shim prefix (static-SDL builds) nothing is bundled.
            tree, _ = package.assemble(bundle, root / 'out-static')
            self.assertFalse((tree / 'melee/libs.aarch64').exists())


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
class ReleaseNotesTests(unittest.TestCase):
    """The release job renders RELEASE_NOTES.md around the built zip; every placeholder must resolve."""
    def setUp(self):
        spec = importlib.util.spec_from_file_location('release_notes', NATIVE / 'tools/release_notes.py')
        self.notes = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.notes)

    def test_template_renders_with_checksum_and_links(self):
        with tempfile.TemporaryDirectory() as tmp:
            fake = Path(tmp) / 'melee.zip'
            fake.write_bytes(b'PK\x05\x06' + bytes(18))
            text = self.notes.render((PORT_DIR / 'RELEASE_NOTES.md').read_text(), fake,
                                     'portmaster-20260919-abcdef0', 'abcdef0123456789abcdef0123456789abcdef01', '2026-09-19')
        self.assertNotIn('{{', text)
        self.assertIn('portmaster-20260919-abcdef0', text)
        self.assertIn('[zalo/melee `abcdef012`](https://github.com/zalo/melee/commit/abcdef0123456789abcdef0123456789abcdef01)', text)
        self.assertIn(hashlib.sha256(b'PK\x05\x06' + bytes(18)).hexdigest(), text)
        self.assertIn('autoinstall', text)
        self.assertIn('melee/assets', text)
        self.assertIn('log.txt', text)

    def test_unknown_placeholder_is_an_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            fake = Path(tmp) / 'melee.zip'
            fake.write_bytes(b'x')
            with self.assertRaises(SystemExit):
                self.notes.render('{{NOPE}}', fake, 't', 'c', 'd')


class BuiltZipTests(unittest.TestCase):
    def test_zip_layout_and_binary(self):
        with zipfile.ZipFile(ZIP) as archive:
            names = set(archive.namelist())
            for name in ['Melee.sh', 'port.json', 'melee/melee.aarch64', 'melee/libs.aarch64/libSDL3.so.0',
                         'melee/licenses/LICENSE.SDL3-sdl2-backend.txt', 'melee/assets/README.txt', 'melee/runtime/']:
                self.assertIn(name, names, name)
            licenses = [n for n in names if n.startswith('melee/licenses/') and not n.endswith('/')]
            self.assertTrue(licenses)
            for name in licenses:
                self.assertRegex(name, r'^melee/licenses/LICENSE\.[^/]+\.txt$')
            self.assertFalse([n for n in names if n.lower().endswith(('.iso', '.gcm', '.ciso', '.rvz', '.img'))])
            self.assertFalse([n for n in names if 'mali' in n.lower() or 'libegl' in n.lower() or 'libgles' in n.lower()])
            self.assertFalse([n for n in names if not (n.startswith('melee/') or n in ('Melee.sh', 'port.json'))])
            binary = archive.read('melee/melee.aarch64')
            self.assertTrue(is_aarch64_elf(binary[:20]))
            # SDL3 is linked dynamically and the shipped libSDL3.so.0 is the SDL2-backend shim, so every
            # CFW's own SDL2 (KMSDRM, fbdev, Wayland; ALSA, PulseAudio, PipeWire) drives the device.
            self.assertIn(b'libSDL3.so.0', binary)
            # SDL3's KMSDRM driver reads this hint; the string exists only when that driver is compiled in.
            self.assertNotIn(b'SDL_KMSDRM_DEVICE_INDEX', binary)
            # The display stack is the CFW's SDL2's business: the binary may require the GL driver and
            # libSDL3.so.0, never libdrm/libgbm/libwayland (it would not load where SDL2 runs on fbdev).
            if shutil.which('readelf'):
                with tempfile.NamedTemporaryFile(suffix='.aarch64') as elf:
                    elf.write(binary)
                    elf.flush()
                    dynamic = subprocess.run(['readelf', '-d', elf.name], capture_output=True, text=True, check=True).stdout
                needed = set(re.findall(r'Shared library: \[([^\]]+)\]', dynamic))
                self.assertIn('libSDL3.so.0', needed)
                self.assertIn('libEGL.so.1', needed)
                self.assertFalse(needed & {'libdrm.so.2', 'libgbm.so.1', 'libwayland-client.so.0', 'libwayland-egl.so.1',
                                           'libSDL2-2.0.so.0', 'libstdc++.so.6', 'libmali.so.1'}, needed)
            shim = archive.read('melee/libs.aarch64/libSDL3.so.0')
            self.assertTrue(is_aarch64_elf(shim[:20]))
            for needle in [b'SDL3SHIM_SDL2_LIB', b'SDL3SHIM_SDL2_VIDEODRIVER', b'libSDL2-2.0.so.0']:
                self.assertIn(needle, shim, needle)
            self.assertFalse([n for n in names if 'libstdc++' in n])
            self.assertEqual([n for n in names if n.startswith('melee/libs.aarch64/') and not n.endswith('/')],
                             ['melee/libs.aarch64/libSDL3.so.0'])
            self.assertEqual(json.loads(archive.read('port.json')), json.loads((PORT_DIR / 'port.json').read_text()))
            self.assertEqual(archive.read('Melee.sh'), (PORT_DIR / 'Melee.sh').read_bytes())
        tree = ZIP.parent / 'melee'
        if tree.exists():
            for name in ['port.json', 'README.md', 'gameinfo.xml', 'screenshot.jpg', 'Melee.sh', 'melee/melee.aarch64']:
                self.assertTrue((tree / name).exists(), name)


if __name__ == '__main__':
    unittest.main()
