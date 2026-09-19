#!/usr/bin/env python3
"""Prepare the pinned Flip SDK, device libraries, patched Dawn source and the Aurora checkout.

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
# Dawn: encounter/dawn base plus native/platform/flip/dawn-gl-interop.patch, which is
# the gl-native-interop branch of https://github.com/zalo/dawn-aurora-arm.git (tip
# DAWN_INTEROP_REV: EGL native-window surface source, swapchain GL storage reuse,
# native OpenGL interop extension) as one diff against DAWN_REV.
DAWN_REV = '1155e0ed531126f33a1279afa029349651ca1c93'
DAWN_INTEROP_REV = 'e1a71853ea57ce918b2dbb2cd4cc71fec7ca04d9'
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


# Device libraries for builds without a device (CI): Debian bookworm arm64 packages providing the
# same sonames the Flip's /usr/lib has. Only libdrm is linked; glvnd's libEGL/libGLESv2 stand in for
# the Mali wrappers (native/CMakeLists.txt links them directly when no libmali is present), and
# libgbm/libasound/libudev only satisfy SDL's configure checks (dlopened at run time).
DEVICE_PACKAGES = {
    'libd/libdrm/libdrm2_2.4.114-1+b1_arm64.deb': 'f5f15a46d02cf5d9fa52d4f1c54b8cf80c398711ad771a9938b12399b8d8090c',
    'm/mesa/libgbm1_22.3.6-1+deb12u2_arm64.deb': 'f5c8fdddbf365259d74af270fb10f30d7fddb3fbe7b2ff62f0fdd556f8db0dc8',
    'libg/libglvnd/libegl1_1.6.0-1_arm64.deb': '707097a275155c600e2e9251c4ee7cdfdb2d8f50a678a850a2526a7bc9664166',
    'libg/libglvnd/libgles2_1.6.0-1_arm64.deb': 'efeb2717380f8411d66b3f669bb99bbd83ff09db3d4a1661533aa31af659608c',
    'a/alsa-lib/libasound2_1.2.8-1+b1_arm64.deb': '9fa889400fcee4b92c8f4a2fafbb7f2cd33444d9ec1665a71002ab67c06114bb',
    's/systemd/libudev1_252.39-1~deb12u2_arm64.deb': 'b76444b0259abfa416f8c1d9a652d208a24770667a1181aa52317b74d12a7a72',
}
DEVICE_SONAMES = {'EGL': 'libEGL.so.1', 'GLESv2': 'libGLESv2.so.2', 'gbm': 'libgbm.so.1', 'drm': 'libdrm.so.2',
                  'asound': 'libasound.so.2', 'udev': 'libudev.so.1'}
# Audio client libraries SDL dlopens on PipeWire/PulseAudio CFWs (ROCKNIX exports
# SDL_AUDIODRIVER=pulseaudio system-wide). Fetched in both modes: SDL needs the headers, .pc data and
# the library's SONAME at configure time, and no device has the headers. Nothing is linked.
AUDIO_PACKAGES = {
    'p/pulseaudio/libpulse0_16.1+dfsg1-2+b1_arm64.deb': '26f17e3457c5fce0104a6b2f0efb75a3256b24b32ca7cfda6266373758a930cb',
    'p/pulseaudio/libpulse-dev_16.1+dfsg1-2+b1_arm64.deb': 'dac8f94dc214a77654cc62d0f1611cd4dee9cdefdc816e3056dd9fae5aa5d4a8',
    'p/pipewire/libpipewire-0.3-0_0.3.65-3+deb12u1_arm64.deb': 'f1e20d052a9f5faa04e80907202762d47cbb280cff23715fe7ca40429549a533',
    'p/pipewire/libpipewire-0.3-dev_0.3.65-3+deb12u1_arm64.deb': 'f8addee828a3f5955c2bd3a09c4aaa2fcc86ec6f89d00308c1a035f0b1a8da18',
    'p/pipewire/libspa-0.2-dev_0.3.65-3+deb12u1_arm64.deb': '2e1c68e265c308c41c89818ac08931de6d8be759eafcfb87a559bd8643ff9a23',
}
AUDIO_SONAMES = {'pulse': 'libpulse.so.0', 'pipewire-0.3': 'libpipewire-0.3.so.0'}
AUDIO_HEADERS = ['pulse', 'pipewire-0.3', 'spa-0.2']


def fetch_debian_packages(packages, usr, sonames, headers=()):
    """Extract Debian arm64 packages into <sysroot>/usr: lib*.so* into lib/, the named include
    directories into include/. Existing files are kept (device-pulled libraries win)."""
    import io
    import sys
    import tarfile
    import tempfile
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import prepare_wayland
    lib = usr / 'lib'
    lib.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as scratch:
        root = Path(scratch)
        for path, expected in packages.items():
            with urllib.request.urlopen(prepare_wayland.MIRROR + path, timeout=120) as response:
                deb = response.read()
            digest = hashlib.sha256(deb).hexdigest()
            if digest != expected:
                raise RuntimeError(f'{path}: sha256 {digest}, expected {expected}')
            with tarfile.open(fileobj=io.BytesIO(prepare_wayland.deb_data(deb))) as archive:
                archive.extractall(root, filter='tar')
        for entry in sorted((root / 'usr/lib/aarch64-linux-gnu').iterdir()):
            if entry.name.startswith('lib') and '.so' in entry.name:
                target = lib / entry.name
                if target.exists() or target.is_symlink():
                    continue
                if entry.is_symlink():
                    target.symlink_to(os.readlink(entry))
                else:
                    shutil.copy2(entry, target)
        for name in headers:
            source = root / 'usr/include' / name
            if not source.is_dir():
                raise RuntimeError(f'{name}: header directory missing from the Debian packages')
            shutil.copytree(source, usr / 'include' / name, dirs_exist_ok=True)
    for stem, soname in sonames.items():
        if not (lib / soname).exists():
            raise RuntimeError(f'{soname} missing after extracting the Debian packages')
        link = lib / ('lib' + stem + '.so')
        if not link.exists():
            link.symlink_to(soname)


def fetch_device_libraries(usr):
    """Populate <sysroot>/usr/lib with the Debian arm64 libraries listed in DEVICE_PACKAGES."""
    fetch_debian_packages(DEVICE_PACKAGES, usr, DEVICE_SONAMES)


def fetch_audio_libraries(usr):
    """PulseAudio and PipeWire client headers, .pc data and libraries for SDL's dlopen audio backends."""
    fetch_debian_packages(AUDIO_PACKAGES, usr, AUDIO_SONAMES, AUDIO_HEADERS)


# pkg-config data SDL's KMSDRM, PulseAudio and PipeWire backends need at configure time (it dlopens
# the libraries at run time). The device libraries come without .pc files, and Debian's carry host
# paths; these point at the sysroot they sit in. build.sh gives pkg-config only these directories.
def _pc(name, version, cflags, libs, requires=''):
    text = f'prefix=${{pcfiledir}}/../..\nName: {name}\nDescription: sysroot {name} (dlopened by SDL)\nVersion: {version}\n'
    if requires:
        text += f'Requires: {requires}\n'
    return text + f'Libs: -L${{prefix}}/lib {libs}\nCflags: {cflags}\n'


SYSROOT_PC = {
    'libdrm': _pc('libdrm', '2.4.114', '-I${prefix}/include -I${prefix}/include/libdrm', '-ldrm'),
    'gbm': _pc('gbm', '22.3.6', '-I${prefix}/include', '-lgbm'),
    'libpulse': _pc('libpulse', '16.1', '-I${prefix}/include -D_REENTRANT', '-lpulse'),
    'libspa-0.2': _pc('libspa-0.2', '0.3.65', '-I${prefix}/include/spa-0.2 -D_REENTRANT', ''),
    'libpipewire-0.3': _pc('libpipewire-0.3', '0.3.65', '-I${prefix}/include/pipewire-0.3 -D_REENTRANT',
                           '-lpipewire-0.3', 'libspa-0.2'),
}


def write_sysroot_pkgconfig(usr):
    """Write every SYSROOT_PC file that is missing under <sysroot>/usr/lib/pkgconfig."""
    pkgconfig = usr / 'lib/pkgconfig'
    pkgconfig.mkdir(parents=True, exist_ok=True)
    for name, text in SYSROOT_PC.items():
        path = pkgconfig / f'{name}.pc'
        if not path.exists():
            path.write_text(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', default='adb')
    parser.add_argument('--device', default='10.0.0.178:5555')
    parser.add_argument('--no-device', action='store_true',
                        help='Take the device libraries from Debian arm64 packages instead of pulling them over ADB')
    parser.add_argument('--cpu', choices=['a55', 'a35'], default='a55',
                        help='Dawn target: a55 (Flip tuning; build/flip-dawn, dawn-install) or '
                             'a35 (PortMaster baseline; build/flip-dawn-a35, dawn-install-a35)')
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
    # Aurora is the zalo/aurora-arm gles-direct-submission checkout (bootstrap.py); it already
    # carries the Flip platform hunks, so no aurora patch is applied here any more.
    subprocess.run(['python3', str(ROOT / 'native/tools/bootstrap.py')], check=True)
    apply_patch(dawn, ROOT / 'native/platform/flip/dawn-gl-interop.patch')
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
    write_sysroot_pkgconfig(usr)
    fetch_audio_libraries(usr)
    if args.no_device:
        fetch_device_libraries(usr)
    else:
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
    suffix = '' if args.cpu == 'a55' else '-' + args.cpu
    toolchain_file = ROOT / 'native/platform/flip' / ('toolchain.cmake' if args.cpu == 'a55' else f'toolchain-{args.cpu}.cmake')
    dawn_build = ROOT / ('build/flip-dawn' + suffix)
    dawn_prefix = TOOLS / ('dawn-install' + suffix)
    if not args.prepare_only:
        subprocess.run(['cmake', '-S', str(dawn), '-B', str(dawn_build),
                        '-DCMAKE_TOOLCHAIN_FILE=' + str(toolchain_file),
                        '-DCMAKE_BUILD_TYPE=Release', '-DDAWN_ENABLE_OPENGLES=ON',
                        '-DDAWN_ENABLE_DESKTOP_GL=OFF', '-DDAWN_ENABLE_VULKAN=OFF',
                        '-DDAWN_USE_X11=OFF', '-DDAWN_USE_WAYLAND=OFF', '-DDAWN_USE_GLFW=OFF',
                        '-DDAWN_BUILD_TESTS=OFF', '-DDAWN_BUILD_SAMPLES=OFF', '-DTINT_BUILD_TESTS=OFF',
                        '-DTINT_BUILD_CMD_TOOLS=OFF', '-DDAWN_BUILD_PROTOBUF=OFF', '-DTINT_BUILD_IR_BINARY=OFF',
                        '-DDAWN_BUILD_MONOLITHIC_LIBRARY=STATIC', '-DDAWN_ENABLE_INSTALL=ON',
                        '-DDAWN_FETCH_DEPENDENCIES=ON'], env=env, check=True)
        subprocess.run(['cmake', '--build', str(dawn_build), '--target', 'webgpu_dawn',
                        '--parallel', os.environ.get('BUILD_JOBS', '6')], env=env, check=True)
        subprocess.run(['cmake', '--install', str(dawn_build), '--prefix', str(dawn_prefix)], env=env, check=True)
    print(f'FLIP_TOOLCHAIN={sdk}')
    print(f'FLIP_DAWN_PREFIX={dawn_prefix}')


if __name__ == '__main__':
    main()
