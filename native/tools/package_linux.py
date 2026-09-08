#!/usr/bin/env python3
"""Package an allowlisted Linux runtime and notices, never the build tree/assets."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[2]


def run(*args, **kw):
    return subprocess.check_output(args, text=True, **kw).strip()


def package(build, destination):
    build, destination = build.resolve(), destination.resolve()
    if re.search(r'^MELEE_SANITIZE_GAME:BOOL=(ON|TRUE|1)$', (build / 'CMakeCache.txt').read_text(), re.M):
        raise RuntimeError('Build the package without MELEE_SANITIZE_GAME; component tests remain instrumented')
    for config in (build / '_deps/sdl-build').glob('include-config-*/build_config/SDL_build_config.h'):
        if re.search(r'#define SDL_\w+_DYNAMIC\w* \"[^\"]+\.a\"', config.read_text()):
            raise RuntimeError(f'SDL cached a static archive as a runtime library: {config}; repair dependencies and reconfigure')
    bundle = destination / 'Melee-Native-Linux-x86_64'
    bundle.mkdir(parents=True, exist_ok=False)
    (bundle / 'lib').mkdir()
    binary = bundle / 'melee_native'
    shutil.copy2(build / 'melee_native', binary)
    # Drivers, libc, desktop and audio services remain supplied by the OS.
    bundled = re.compile(r'lib(?:png16|freetype|zstd|z|bz2|brotlidec|brotlicommon|fmt|SDL3|xxhash)\.so')
    system = re.compile(r'(?:linux-vdso|ld-linux|lib(?:c|m|dl|pthread|rt|stdc\+\+|gcc_s|resolv)\.so)')
    copied = {}
    pending = [binary]
    while pending:
        file = pending.pop()
        dependencies = run('ldd', str(file))
        if 'not found' in dependencies:
            raise RuntimeError(dependencies)
        for line in dependencies.splitlines():
            fields = line.split()
            if not fields:
                continue
            name = fields[0]
            if 'clang_rt' in name or 'asan' in name or 'ubsan' in name:
                raise RuntimeError(f'Sanitized runtime cannot be packaged: {name}')
            if bundled.match(name):
                source = Path(fields[2]).resolve(strict=True)
                if name in copied:
                    if copied[name] != source:
                        raise RuntimeError(f'Library collision: {name}')
                    continue
                copied[name] = source
                target = bundle / 'lib' / name
                shutil.copy2(source, target)
                pending.append(target)
            elif not system.match(Path(name).name):
                raise RuntimeError(f'Unclassified runtime dependency: {line}')
    for file in [binary, *list((bundle / 'lib').iterdir())]:
        run('patchelf', '--set-rpath', '$ORIGIN/lib' if file == binary else '$ORIGIN', str(file))
    (bundle / 'melee-native').write_text('''#!/bin/sh
# Resolve symlinks so /usr/bin and relocated bundles both work.
app_dir=$(dirname "$(readlink -f "$0")")
exec "$app_dir/melee_native" "$@"
''')
    (bundle / 'melee-native').chmod(0o755)
    shutil.copy2(ROOT / 'native/linux-package-readme.txt', bundle / 'README.txt')
    shutil.copy2(ROOT / 'native/packaging/melee-native.desktop', bundle)
    notices = bundle / 'Third Party Notices'
    notices.mkdir()
    sources = {
        'Aurora.txt': ROOT / 'build/native-deps/aurora/LICENSE',
        'SDL.txt': build / '_deps/sdl-src/LICENSE.txt',
        'ImGui.txt': build / '_deps/imgui-src/LICENSE.txt',
        'Abseil.txt': build / '_deps/abseil-cpp-src/LICENSE',
        'Tracy.txt': build / '_deps/tracy-src/LICENSE',
        'xxHash.txt': build / '_deps/xxhash-src/LICENSE',
        'Dawn.txt': ROOT / 'native/licenses/Dawn.txt',
        'nod.txt': ROOT / 'native/licenses/nod.txt',
        'FreeType.txt': ROOT / 'native/licenses/FreeType.txt',
    }
    fmt = build / '_deps/fmt-src/LICENSE'
    if fmt.exists():
        sources['fmt.txt'] = fmt
    # Distribution copyright files include library-specific and bundled licenses.
    doc = Path(os.environ.get('MELEE_SYSTEM_DOC', '/usr/share/doc'))
    notice_packages = {'libpng16': 'libpng16-16t64', 'libfreetype': 'libfreetype6',
                       'libzstd': 'libzstd1', 'libz.so': 'zlib1g', 'libbz2': 'libbz2-1.0',
                       'libbrotli': 'libbrotli1', 'libfmt': 'libfmt-dev'}
    arch_licenses = {'libpng16-16t64': 'libpng', 'libfreetype6': 'freetype2',
                     'libzstd1': 'zstd', 'zlib1g': 'zlib', 'libbz2-1.0': 'bzip2',
                     'libbrotli1': 'brotli', 'libfmt-dev': 'fmt'}
    for prefix, pkg in notice_packages.items():
        if any(name.startswith(prefix) for name in copied):
            path = doc / pkg / 'copyright'
            if not path.exists():
                path = Path('/usr/share/doc') / pkg / 'copyright'
            if path.exists():
                sources[pkg + '.txt'] = path
            else:
                license_dir = Path('/usr/share/licenses') / arch_licenses[pkg]
                files = [p for p in license_dir.rglob('*') if p.is_file()]
                if not files:
                    raise RuntimeError(f'Missing notices for {pkg}; set MELEE_SYSTEM_DOC to a notice staging directory')
                for i, license_file in enumerate(files):
                    sources[f'{pkg}-{i}.txt'] = license_file
    for name, source in sources.items():
        shutil.copy2(source, notices / name)
    magic = (ROOT / 'build/native-deps/aurora/include/magic_enum.hpp').read_text()
    (notices / 'magic_enum.txt').write_text(magic.split('#ifndef', 1)[0])
    sqlite = (build / '_deps/sqlite3-src/sqlite3.h').read_text()
    (notices / 'SQLite.txt').write_text(sqlite.split('*/', 1)[0] + '*/\n')
    (notices / 'Melee-upstream.txt').write_text(
        'Original recovered game source: https://github.com/doldecomp/melee\n'
        'Native fork: https://github.com/jonrosner/melee-native\n'
        'Existing source notices remain in the source distribution.\n'
        'Removing game assets does not establish rights to redistribute recovered code.\n')
    manifest = {str(p.relative_to(bundle)): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in sorted(bundle.rglob('*')) if p.is_file()}
    (destination / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    archive = destination / 'Melee-Native-Linux-x86_64.tar.gz'
    with tarfile.open(archive, 'w:gz') as tar:
        tar.add(bundle, arcname=bundle.name)
    # Check relocation without development library search paths.
    env = os.environ.copy()
    env.pop('LD_LIBRARY_PATH', None)
    result = run('ldd', str(binary), env=env)
    if 'not found' in result or str(build) in result:
        raise RuntimeError(f'Nonportable package: {result}')
    print(f'{bundle}\n{archive}\n{len(manifest)} allowlisted files; no game assets')
    return bundle


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path, nargs='?', default=ROOT / 'build/native-linux')
    parser.add_argument('destination', type=Path, nargs='?', default=ROOT / 'dist/linux')
    args = parser.parse_args()
    package(args.build, args.destination)
