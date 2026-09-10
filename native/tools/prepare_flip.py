#!/usr/bin/env python3
"""Prepare the pinned Flip SDK, device libraries, and patched Dawn/Aurora sources.

Requires Linux host development headers (EGL, GLES3, GBM, DRM, ALSA, udev),
Clang, CMake, Git, Python 3.11+, and ADB. No administrator access is used.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / 'build/flip-tools'
DAWN_REV = '1155e0ed531126f33a1279afa029349651ca1c93'
SDK_NAME = 'aarch64--glibc--stable-2023.08-1'


def fetch_archive(filename, url, checksum, expected):
    if expected.is_dir():
        return
    TOOLS.mkdir(parents=True, exist_ok=True)
    archive = TOOLS / filename
    if not archive.is_file():
        urllib.request.urlretrieve(url, archive)
    with archive.open('rb') as f:
        actual = hashlib.file_digest(f, 'sha256').hexdigest()
    if actual != checksum:
        raise RuntimeError(f'Checksum mismatch: {archive}; remove the incomplete download before retrying')
    with tarfile.open(archive) as tar:
        tar.extractall(TOOLS, filter='data')
    if not expected.is_dir():
        raise RuntimeError(f'Archive did not produce {expected}')


def apply_patch(directory, patch):
    directory = Path(directory).resolve()
    patch = Path(patch).resolve()
    # Dawn is an extracted archive, not a Git checkout. Without this boundary,
    # Git discovers the enclosing Melee repository and can silently skip every
    # patch path outside the dependency's prefix, even for --check.
    env = {key: value for key, value in os.environ.items()
           if key not in ('GIT_DIR', 'GIT_WORK_TREE', 'GIT_INDEX_FILE',
                          'GIT_COMMON_DIR', 'GIT_PREFIX')}
    env['GIT_CEILING_DIRECTORIES'] = str(directory.parent)
    command = ['git', '-C', str(directory), 'apply']
    if subprocess.run(command + ['--reverse', '--check', str(patch)], env=env, capture_output=True).returncode == 0:
        return
    subprocess.run(command + ['--check', str(patch)], env=env, check=True)
    subprocess.run(command + [str(patch)], env=env, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', default='adb')
    parser.add_argument('--device', default='10.0.0.178:5555')
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--with-g29', action='store_true', help='Fetch the verified app-local GLES driver')
    args = parser.parse_args()
    if args.with_g29:
        driver = TOOLS / 'mali-g29p1-candidate/libmali.so.1'
        driver.parent.mkdir(parents=True, exist_ok=True)
        if not driver.exists():
            partial = driver.with_suffix('.partial')
            urllib.request.urlretrieve(
                'https://raw.githubusercontent.com/JeffyCN/mirrors/'
                '1a082323f1001874a007e4e522029d6c46d75ae9/'
                'lib/aarch64-linux-gnu/libmali-bifrost-g52-g29p1-gles.so', partial)
            if hashlib.sha256(partial.read_bytes()).hexdigest() != '02d42b4a49007d8b99865653105783034df9adbd502ccef035038a0c3756c027':
                raise RuntimeError('Downloaded Mali driver checksum mismatch')
            partial.rename(driver)
        if hashlib.sha256(driver.read_bytes()).hexdigest() != '02d42b4a49007d8b99865653105783034df9adbd502ccef035038a0c3756c027':
            raise RuntimeError('Cached Mali driver checksum mismatch')

    sdk = TOOLS / SDK_NAME
    dawn = TOOLS / ('dawn-' + DAWN_REV)
    fetch_archive('toolchain.tar.bz2',
                  f'https://toolchains.bootlin.com/downloads/releases/toolchains/aarch64/tarballs/{SDK_NAME}.tar.bz2',
                  'aed4223eadef27c1a84676333cbbdb75cbb5ee5a4a0cfc3ec5a491c6a6179de8', sdk)
    fetch_archive('dawn-source.tar.gz', f'https://github.com/encounter/dawn/archive/{DAWN_REV}.tar.gz',
                  'd0d291936d02a56b3b7e92e84e8b8c71db04a9c9e2b3a41cc6d7540cf13b4167', dawn)
    subprocess.run(['python3', str(ROOT / 'native/tools/bootstrap.py')], check=True)
    apply_patch(ROOT / 'build/native-deps/aurora', ROOT / 'native/platform/flip/aurora-flip.patch')
    apply_patch(dawn, ROOT / 'native/platform/flip/dawn-egl-native-window.patch')
    usr = sdk / 'aarch64-buildroot-linux-gnu/sysroot/usr'
    headers = Path(os.environ.get('FLIP_HOST_HEADERS', '/usr/include'))
    for name in ['EGL', 'GLES3', 'KHR', 'libdrm', 'alsa', 'gbm.h', 'xf86drm.h', 'xf86drmMode.h', 'libudev.h']:
        src = headers / name
        if not src.exists():
            raise RuntimeError(f'Missing host development header: {src}')
        if src.is_dir():
            shutil.copytree(src, usr / 'include' / name, dirs_exist_ok=True)
        else:
            shutil.copy2(src, usr / 'include' / name)
    subprocess.run([args.adb, 'connect', args.device], check=True)
    # Resolve device symlinks while pulling, and provide normal linker names.
    for stem, soname in {'EGL': 'libEGL.so.1', 'GLESv2': 'libGLESv2.so.2',
                         'gbm': 'libgbm.so.1', 'drm': 'libdrm.so.2',
                         'mali': 'libmali.so.1', 'mali_hook': 'libmali_hook.so.1',
                         'asound': 'libasound.so.2', 'udev': 'libudev.so.1'}.items():
        target = usr / 'lib' / soname
        if not target.exists():
            subprocess.run([args.adb, '-s', args.device, 'pull', '/usr/lib/' + soname, str(target)], check=True)
        link = usr / 'lib' / ('lib' + stem + '.so')
        if not link.exists():
            link.symlink_to(soname)
    env = dict(os.environ, FLIP_TOOLCHAIN=str(sdk))
    if not args.prepare_only:
        subprocess.run(['cmake', '-S', str(dawn), '-B', str(ROOT / 'build/flip-dawn'),
                        '-DCMAKE_TOOLCHAIN_FILE=' + str(ROOT / 'native/platform/flip/toolchain.cmake'),
                        '-DCMAKE_BUILD_TYPE=Release', '-DDAWN_ENABLE_OPENGLES=ON',
                        '-DDAWN_ENABLE_DESKTOP_GL=OFF', '-DDAWN_ENABLE_VULKAN=OFF',
                        '-DDAWN_USE_X11=OFF', '-DDAWN_USE_WAYLAND=OFF', '-DDAWN_USE_GLFW=OFF',
                        '-DDAWN_BUILD_TESTS=OFF', '-DDAWN_BUILD_SAMPLES=OFF', '-DTINT_BUILD_TESTS=OFF',
                        '-DTINT_BUILD_CMD_TOOLS=OFF', '-DDAWN_BUILD_PROTOBUF=OFF', '-DTINT_BUILD_IR_BINARY=OFF',
                        '-DDAWN_BUILD_MONOLITHIC_LIBRARY=STATIC', '-DDAWN_ENABLE_INSTALL=ON',
                        '-DDAWN_FETCH_DEPENDENCIES=ON'], env=env, check=True)
        subprocess.run(['cmake', '--build', str(ROOT / 'build/flip-dawn'), '--target', 'webgpu_dawn',
                        '--parallel', os.environ.get('BUILD_JOBS', '6')], env=env, check=True)
        subprocess.run(['cmake', '--install', str(ROOT / 'build/flip-dawn'), '--prefix', str(TOOLS / 'dawn-install')],
                       env=env, check=True)
    print(f'FLIP_TOOLCHAIN={sdk}')
    print(f'FLIP_DAWN_PREFIX={TOOLS / "dawn-install"}')


if __name__ == '__main__':
    main()
