#!/usr/bin/env python3
"""Validate the PortMaster port files, the launcher, and (when built) the zip."""
import gzip
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
        self.assertEqual(self.port['items'], ['Melee.sh', 'Melee Soak Test.sh', 'melee'])
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
                       'export SDL_VIDEODRIVER=sdl2',
                       'if [ "$CFW_NAME" = "ROCKNIX" ]; then',
                       'export SDL3SHIM_SDL2_VIDEODRIVER=wayland']:
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

    def run_launcher(self, mode='normal', disc=True, cfw_env=(), cfw_name='testcfw', soak=False, save=True,
                     before=None, start=None):
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
CFW_NAME={cfw_name}
GPTOKEYB="{root}/gptokeyb-classic"
GPTOKEYB2="{root}/gptokeyb"
# PortMaster's funcs.txt guards itself with an exported variable, as here: a launcher started from
# inside another launcher finds none of the functions below unless it clears the guard.
if [ -n "$PM_FUNCS_VERSION" ]; then return; fi
export PM_FUNCS_VERSION=2
get_controls() {{ sdl_controllerconfig="fake-map"; }}
pm_message() {{ printf '%s\\n' "$1" > "{root}/message"; }}
pm_platform_helper() {{ printf '%s\\n' "$1" > "{root}/helper"; }}
pm_finish() {{ printf finished > "{root}/finished"; }}
''')
        (control / f'mod_{cfw_name}.txt').write_text(f'printf sourced > "{root}/mod"\n')
        (root / 'gptokeyb').write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$GPTOKEYB_LOG"\nexit 0\n')
        (root / 'gptokeyb').chmod(0o755)
        if disc:
            (gamedir / 'assets').mkdir()
            (gamedir / 'assets/Melee (USA) (v1.02).RVZ').write_bytes(b'not a disc')
        text = (PORT_DIR / 'Melee.sh').read_text()
        text = text.replace('controlfolder="/roms/ports/PortMaster"', f'controlfolder="{control}"')
        launcher = gamedir.parent / 'Melee.sh'
        launcher.write_text(text)
        game = gamedir / 'melee.aarch64'
        # Zip extraction drops the exec bit; the launcher must restore it.
        game.write_text(f'''#!/bin/sh
printf '%s\\n' "$1" > "{root}/disc"
printf '%s\\n' "$LD_LIBRARY_PATH" "$XDG_CONFIG_HOME" "$XDG_STATE_HOME" "$XDG_CACHE_HOME" "$SDL_GAMECONTROLLERCONFIG" "$SDL_VIDEODRIVER" "$SDL_AUDIODRIVER" "${{SDL3SHIM_SDL2_VIDEODRIVER:-unset}}" "${{SDL3SHIM_SDL2_AUDIODRIVER:-unset}}" > "{root}/env"
[ "$GAME_MODE" = fail ] && exit 7
[ "$GAME_MODE" = nodriver ] && echo "[launch] No usable graphics driver: neither OpenGL ES nor Vulkan could be started on this system, so Melee cannot run here" >&2 && exit 1
[ "$GAME_MODE" = slow ] && /bin/sleep 2
[ "$GAME_MODE" = quit ] && echo "[exit] quit requested" >&2
exit 0
''')
        game.chmod(0o644)
        launcher_name = start or (package.SOAK_LAUNCHER if soak else 'Melee.sh')
        if soak:
            # The soak launcher sits beside Melee.sh; its pauses are shortened through a fake sleep.
            shutil.copyfile(PORT_DIR / package.SOAK_LAUNCHER, gamedir.parent / package.SOAK_LAUNCHER)
            shutil.copytree(PORT_DIR / 'soak', gamedir / 'soak')
            if save:
                (gamedir / 'runtime/config/melee-native').mkdir(parents=True)
                (gamedir / 'runtime/config/melee-native/save').write_text('real save')
            fake_bin = root / 'bin'
            fake_bin.mkdir()
            (fake_bin / 'sleep').write_text('#!/bin/sh\nexec /bin/sleep 0.05\n')
            (fake_bin / 'sleep').chmod(0o755)
            # A download (-o) is served from <root>/release; anything else is a report being posted.
            (fake_bin / 'curl').write_text(f'''#!/bin/sh
printf '%s\\n' "$@" >> "{root}/curl"
for url; do :; done
while [ $# -gt 0 ]; do [ "$1" = -o ] && exec cp "{root}/release/$(basename "$url")" "$2"; shift; done
echo "ok abc123"
''')
            (fake_bin / 'curl').chmod(0o755)
            cfw_env['PATH'] = f"{fake_bin}:{os.environ['PATH']}"
        if before:
            before(gamedir)
        env = dict(os.environ, GAME_MODE=mode, GPTOKEYB_LOG=str(root / 'gptokeyb.log'), HOME=str(root))
        for name in ['SDL_VIDEODRIVER', 'SDL_AUDIODRIVER', 'SDL3SHIM_SDL2_VIDEODRIVER', 'SDL3SHIM_SDL2_AUDIODRIVER']:
            env.pop(name, None)
        env.update(cfw_env)
        result = subprocess.run(['bash', str(gamedir.parent / launcher_name)], env=env, stdout=subprocess.DEVNULL,
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
        self.assertEqual(env[1:4], [str(gamedir / 'runtime/config'), str(gamedir / 'runtime/state'),
                                    str(gamedir / 'runtime/cache')])
        # get_controls' map is handed to the game as-is (the CFW's SDL2 owns the pad mapping).
        self.assertEqual(env[4], 'fake-map')
        # SDL3 uses the shim's "sdl2" driver; with no CFW driver names and no ROCKNIX pin the inner
        # SDL2 auto-picks display and audio (SDL_AUDIODRIVER and both shim overrides stay unset).
        self.assertEqual(env[5], 'sdl2')
        self.assertEqual(env[6:9], ['', 'unset', 'unset'])
        self.assertEqual((root / 'gptokeyb.log').read_text().split(),
                         ['melee.aarch64', '-c', str(gamedir / 'melee.ini')])
        self.assertEqual((root / 'helper').read_text().strip(), str(gamedir / 'melee.aarch64'))
        self.assertTrue((root / 'finished').exists())
        self.assertTrue((gamedir / 'log.txt').exists())

    def test_forces_shim_video_driver(self):
        # Whatever SDL video driver the CFW exported, the game's SDL3 uses the shim's "sdl2" driver;
        # the inner SDL2 then autodetects display and audio (no shim overrides on a non-ROCKNIX CFW).
        root, gamedir, result = self.run_launcher(
            cfw_env={'SDL_VIDEODRIVER': 'wayland', 'SDL_AUDIODRIVER': 'pulseaudio'})
        self.assertEqual(result, 0)
        env = (root / 'env').read_text().splitlines()
        self.assertEqual(env[5], 'sdl2')
        self.assertEqual(env[7:9], ['unset', 'unset'])

    def test_rocknix_pins_inner_wayland(self):
        # ROCKNIX's inner SDL2 does not autodetect its Wayland display, so the launcher pins it.
        root, gamedir, result = self.run_launcher(cfw_name='ROCKNIX')
        self.assertEqual(result, 0)
        env = (root / 'env').read_text().splitlines()
        self.assertEqual(env[5], 'sdl2')
        self.assertEqual(env[7], 'wayland')

    def test_missing_disc_reports_and_exits(self):
        root, gamedir, result = self.run_launcher(disc=False)
        self.assertEqual(result, 1)
        self.assertIn('melee/assets', (root / 'message').read_text())
        self.assertFalse((root / 'disc').exists())

    def test_game_failure_still_finishes(self):
        root, gamedir, result = self.run_launcher(mode='fail')
        self.assertTrue((root / 'disc').exists())
        self.assertTrue((root / 'finished').exists())
        self.assertIn('exited unexpectedly', (root / 'message').read_text())

    def test_no_graphics_driver_is_named(self):
        # The game leaves with status 1 and one line in the log when no graphics backend starts.
        root, gamedir, result = self.run_launcher(mode='nodriver')
        self.assertIn('graphics driver cannot run Melee', (root / 'message').read_text())
        self.assertTrue((root / 'finished').exists())

    def read_soak_report(self, gamedir):
        return gzip.decompress((gamedir / 'soak-report.txt.gz').read_bytes()).decode()

    def test_soak_runs_on_a_save_copy_and_writes_a_report(self):
        root, gamedir, result = self.run_launcher(mode='fail', soak=True)
        env = (root / 'env').read_text().splitlines()
        # The puppet's save is a copy; the real one is neither used nor changed.
        self.assertEqual(env[1], str(gamedir / 'runtime/soak-config'))
        self.assertEqual((gamedir / 'runtime/soak-config/melee-native/save').read_text(), 'real save')
        self.assertEqual((gamedir / 'runtime/config/melee-native/save').read_text(), 'real save')
        report = self.read_soak_report(gamedir)
        self.assertTrue(report.startswith('melee soak report v1\nresult: CRASHED (exit status 7)\n'), report[:200])
        self.assertIn('modes: classic,adventure,allstar', report)
        self.assertIn('version: development build\n', report)
        self.assertIn('== log ==', report)
        # The report goes to the address that ships in soak/; the message shows the receiver's id.
        sent = (root / 'curl').read_text()
        self.assertIn('https://melee-reports.sels.tech/report\n', sent)
        self.assertIn(f'@{gamedir}/soak-report.txt.gz\n', sent)
        self.assertIn('id abc123', (root / 'message').read_text())
        # A build without melee/version.txt never looks for an update.
        self.assertNotIn('melee-version.txt', sent)
        self.assertFalse((gamedir / 'soak/running').exists())
        self.assertTrue((root / 'finished').exists())

    def test_soak_time_limit_and_offline_switch(self):
        # melee/soak/offline keeps the test off the network: no update check, no report sent.
        def offline(gamedir):
            (gamedir / 'soak/offline').write_text('')
            (gamedir / 'version.txt').write_text('portmaster-20260101-aaaaaaa\n')
        root, gamedir, result = self.run_launcher(mode='slow', soak=True, before=offline,
                                                  cfw_env={'MELEE_SOAK_MINUTES': '1'})
        self.assertIn('result: completed 1 minutes', self.read_soak_report(gamedir))
        self.assertFalse((root / 'curl').exists())
        self.assertIn('soak-report.txt.gz - please share', (root / 'message').read_text())

    def test_soak_stopped_from_inside_the_game(self):
        # Start+Select also reaches the game, which then leaves by itself with status 0.
        root, gamedir, result = self.run_launcher(mode='quit', soak=True)
        self.assertIn('result: stopped by the exit hotkey\n', self.read_soak_report(gamedir))

    OLD, NEW = 'portmaster-20261001-aaaaaaa', 'portmaster-20261002-bbbbbbb'

    def release(self, root, gamedir, tag=NEW, game='exit 0', checksum=None):
        """Lays out <root>/release as the newest GitHub release and marks the install as OLD."""
        (gamedir / 'version.txt').write_text(self.OLD + '\n')
        directory = root / 'release'
        directory.mkdir()
        with zipfile.ZipFile(directory / 'melee.zip', 'w') as archive:
            archive.writestr('Melee.sh', (gamedir.parent / 'Melee.sh').read_text() + '# new launcher\n')
            archive.writestr('Melee Soak Test.sh', (PORT_DIR / 'Melee Soak Test.sh').read_text())
            archive.writestr('port.json', '{}')
            archive.writestr('melee/melee.aarch64', f'#!/bin/sh\necho ran > "{root}/new-game"\n{game}\n')
            archive.writestr('melee/version.txt', tag + '\n')
            archive.writestr('melee/soak/soak.sh', (PORT_DIR / 'soak/soak.sh').read_text())
        digest = hashlib.sha256((directory / 'melee.zip').read_bytes()).hexdigest()
        (directory / 'melee-version.txt').write_text(f'{tag} {checksum or digest}\n')

    def test_soak_installs_a_newer_release_first(self):
        root, gamedir, result = self.run_launcher(soak=True, before=lambda gamedir: self.release(gamedir.parents[1], gamedir))
        fetched = (root / 'curl').read_text()
        self.assertIn('https://github.com/zalo/melee/releases/latest/download/melee-version.txt\n', fetched)
        self.assertIn('https://github.com/zalo/melee/releases/latest/download/melee.zip\n', fetched)
        # The new launcher and game replaced the old ones and ran; the old build is kept for a rollback.
        self.assertTrue((gamedir.parent / 'Melee.sh').read_text().endswith('# new launcher\n'))
        self.assertTrue((root / 'new-game').exists())
        self.assertFalse((root / 'disc').exists())
        self.assertEqual((gamedir / 'version.txt').read_text(), self.NEW + '\n')
        self.assertEqual((gamedir / 'update/previous/melee/version.txt').read_text(), self.OLD + '\n')
        self.assertFalse((gamedir.parent / 'Melee.sh').read_text() ==
                         (gamedir / 'update/previous/Melee.sh').read_text())
        # PortMaster's own port.json is not the updater's to place, and nothing is left half-written.
        self.assertFalse((gamedir.parent / 'port.json').exists())
        self.assertFalse(list(gamedir.parent.rglob('*.new')))
        self.assertFalse((gamedir / 'update/new').exists())
        report = self.read_soak_report(gamedir)
        self.assertIn(f'version: {self.NEW}\n', report)
        self.assertIn('result: the game exited by itself\n', report)
        self.assertTrue((root / 'finished').exists())

    def test_soak_started_by_the_first_updater(self):
        # Release 9a27a20 updates from inside its launcher: it starts the new Melee.sh directly, in an
        # environment PortMaster's control.txt has already been through.
        root, gamedir, result = self.run_launcher(
            soak=True, start='Melee.sh',
            cfw_env={'MELEE_SOAK': '1', 'MELEE_SOAK_UPDATED': '1', 'PM_FUNCS_VERSION': '2'})
        self.assertTrue((root / 'disc').exists())
        self.assertIn('result: the game exited by itself', self.read_soak_report(gamedir))
        self.assertTrue((root / 'finished').exists())

    def test_soak_restores_the_previous_build_when_an_update_cannot_start(self):
        root, gamedir, result = self.run_launcher(
            soak=True, before=lambda gamedir: self.release(gamedir.parents[1], gamedir, game='exit 9'))
        self.assertTrue((root / 'new-game').exists())
        self.assertEqual((gamedir / 'version.txt').read_text(), self.OLD + '\n')
        self.assertFalse((gamedir.parent / 'Melee.sh').read_text().endswith('# new launcher\n'))
        self.assertIn('printf', (gamedir / 'melee.aarch64').read_text())
        self.assertEqual((gamedir / 'update/rejected').read_text(), self.NEW + '\n')
        self.assertIn(f'result: CRASHED (exit status 9); update {self.NEW} did not start, previous build restored\n',
                      self.read_soak_report(gamedir))

    def test_soak_keeps_the_installed_build_when_an_update_is_not_usable(self):
        cases = {
            'checksum mismatch': dict(checksum='0' * 64),
            'older release': dict(tag='portmaster-20260930-ccccccc'),
            'same release': dict(tag=self.OLD),
            'not a release tag': dict(tag='latest; rm -rf /'),
        }
        for name, arguments in cases.items():
            with self.subTest(name):
                root, gamedir, result = self.run_launcher(
                    soak=True, before=lambda gamedir: self.release(gamedir.parents[1], gamedir, **arguments))
                self.assertEqual((gamedir / 'version.txt').read_text(), self.OLD + '\n')
                self.assertTrue((root / 'disc').exists())
                self.assertFalse((root / 'new-game').exists())
                self.assertFalse((gamedir / 'update/previous').exists())
                self.assertIn(f'version: {self.OLD}\n', self.read_soak_report(gamedir))
        # A release the device rejected before is not downloaded again.
        def rejected(gamedir):
            self.release(gamedir.parents[1], gamedir)
            (gamedir / 'update').mkdir()
            (gamedir / 'update/rejected').write_text(self.NEW + '\n')
        root, gamedir, result = self.run_launcher(soak=True, before=rejected)
        self.assertNotIn('melee.zip', (root / 'curl').read_text())
        self.assertTrue((root / 'disc').exists())

    def test_soak_memory_readers(self):
        script = f'source "{PORT_DIR}/soak/soak.sh"; soak_avail_kb; soak_rss_kb $$'
        values = subprocess.run(['bash', '-c', script], check=True, capture_output=True, text=True,
                                timeout=30).stdout.split()
        self.assertEqual(len(values), 2)
        self.assertTrue(all(value.isdigit() and int(value) > 0 for value in values), values)

    def test_soak_reports_a_run_that_never_finished(self):
        def interrupted(gamedir):
            # What a freeze or a power loss leaves behind: the marker and the log of that run.
            (gamedir / 'soak/running').write_text('adventure\n')
            (gamedir / 'log.txt').write_text('[puppet] match 7 stage=73 character=2 mode=2\n')
        root, gamedir, result = self.run_launcher(soak=True, before=interrupted)
        with gzip.open(gamedir / 'soak-report-unfinished.txt.gz', 'rt') as stream:
            unfinished = stream.read()
        self.assertIn('result: did not finish', unfinished)
        self.assertIn('modes: adventure\n', unfinished)
        self.assertIn('last: [puppet] match 7 stage=73', unfinished)
        # The run that follows gets its own report.
        self.assertIn('result: the game exited by itself', self.read_soak_report(gamedir))

    def test_soak_needs_an_existing_save(self):
        root, gamedir, result = self.run_launcher(soak=True, save=False)
        self.assertEqual(result, 1)
        self.assertIn('normally once', (root / 'message').read_text())
        self.assertFalse((root / 'disc').exists())


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
            tree, zip_path = package.assemble(bundle, root / 'out', sdl3=shim, version='portmaster-20261001-abcdef0')
            self.assertEqual(tree, root / 'out/melee')
            for name in ['port.json', 'Melee.sh', 'Melee Soak Test.sh', 'README.md', 'gameinfo.xml', 'screenshot.jpg',
                         'melee/melee.aarch64', 'melee/melee.ini', 'melee/soak/soak.sh', 'melee/soak/puppet_menu1.txt',
                         'melee/soak/report-url.txt', 'melee/soak/update-url.txt',
                         'melee/licenses/LICENSE.Aurora.txt', 'melee/assets/README.txt']:
                self.assertTrue((tree / name).exists(), name)
            self.assertEqual((tree / 'melee/version.txt').read_text(), 'portmaster-20261001-abcdef0\n')
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
                for name in ['Melee.sh', 'Melee Soak Test.sh', 'port.json', 'melee/melee.aarch64', 'melee/melee.ini',
                             'melee/soak/soak.sh', 'melee/soak/puppet_menu1.txt', 'melee/libs.aarch64/libSDL3.so.0',
                             'melee/licenses/LICENSE.Aurora.txt', 'melee/licenses/LICENSE.SDL3-sdl2-backend.txt',
                             'melee/assets/README.txt', 'melee/runtime/']:
                    self.assertIn(name, names, name)
                self.assertFalse([n for n in names if 'libstdc++' in n])
                self.assertEqual([n for n in names if n.startswith('melee/libs.aarch64/') and not n.endswith('/')],
                                 ['melee/libs.aarch64/libSDL3.so.0'])
                self.assertEqual((archive.getinfo('melee/libs.aarch64/libSDL3.so.0').external_attr >> 16) & 0o777, 0o755)
                self.assertFalse([n for n in names if not (n.startswith('melee/') or n in ('Melee.sh', 'Melee Soak Test.sh', 'port.json'))])
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


class WorkflowTests(unittest.TestCase):
    """The newest build stays downloadable from releases/latest/download/<fixed name>: the notes, the
    README and the soak updater on every installed device all use those links."""
    def test_release_job_publishes_the_latest_release_under_fixed_names(self):
        workflow = (NATIVE.parent / '.github/workflows/portmaster.yml').read_text()
        create = workflow[workflow.index('gh release create'):]
        create = create[:create.index('--notes-file')]
        for asset in ['release/melee.zip', 'release/melee-version.txt', 'release/melee-portmaster-symbols.tar.gz']:
            self.assertIn(asset, create)
        self.assertIn('--latest', create)
        self.assertNotIn('--prerelease', create)
        self.assertNotIn('--draft', create)
        self.assertEqual((PORT_DIR / 'soak/update-url.txt').read_text().strip(),
                         'https://github.com/zalo/melee/releases/latest/download')


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
        self.assertIn('https://github.com/zalo/melee/releases/latest/download/melee.zip', text)
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
            self.assertFalse([n for n in names if not (n.startswith('melee/') or n in ('Melee.sh', 'Melee Soak Test.sh', 'port.json'))])
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
                order = re.findall(r'Shared library: \[([^\]]+)\]', dynamic)
                needed = set(order)
                self.assertIn('libSDL3.so.0', needed)
                # GLES comes first: where it is the whole vendor driver (libmali) the binary's EGL is
                # that library's, the one holding SDL's context, whatever libEGL.so.1 is on the device.
                self.assertLess(order.index('libGLESv2.so.2'), order.index('libEGL.so.1'), order)
                self.assertFalse(needed & {'libdrm.so.2', 'libgbm.so.1', 'libwayland-client.so.0', 'libwayland-egl.so.1',
                                           'libSDL2-2.0.so.0', 'libstdc++.so.6', 'libmali.so.1'}, needed)
                # Math is linked statically: the CFWs' libm versions differ in the last bit of
                # sinf/atan2f, which desyncs online play between devices.
                self.assertNotIn('libm.so.6', needed)
                with tempfile.NamedTemporaryFile(suffix='.aarch64') as elf:
                    elf.write(binary)
                    elf.flush()
                    dynsyms = subprocess.run(['readelf', '--dyn-syms', '-W', elf.name], capture_output=True, text=True, check=True).stdout
                self.assertFalse(re.findall(r' UND (sinf|cosf|atan2f|sqrtf)@', dynsyms))
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
