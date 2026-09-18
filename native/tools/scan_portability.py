#!/usr/bin/env python3
"""Inventory recurring source-port hazards. Matches are candidates, not diagnoses.

Read-only: no automatic edits. Every finding records which preprocessor branch
it sits in (native, original or shared) so the worklist can skip code that only
builds for the GameCube, while the JSON keeps all branches for comparison.
"""
import argparse
import json
import re
from pathlib import Path

RULES = {
    'narrow_integer_cast': r'\((?:u32|s32|int|unsigned int)\)\s*(?:\w|&|\*)',
    'integer_to_pointer': r'\([\w ]+\*+\)\s*(?:\w|\()',
    'address_overlay': r'\([\w ]+\*+\)\s*&\w+|\(\s*\(\s*\w+\s*\*\)\s*&',
    'fixed_byte_offset': r'\((?:u8|char|unsigned char)\s*\*\)[^;\n]{0,80}[+-]\s*0x[0-9A-Fa-f]+',
    'byte_count_as_elements': r'0x[0-9A-Fa-f]+\s*/\s*sizeof\(',
    'fixed_size_copy': r'\b(?:memcpy|memmove|memset|memzero)\([^;\n]+,\s*(?:0x[0-9A-Fa-f]+|[1-9][0-9]*)\s*\)',
    'signed_shift': r'\b1\s*<<|\(s(?:8|16|32)\)[^;\n]{0,50}<<',
    'float_to_small_integer': r'\((?:u8|s8|u16|s16)\)\s*\([^;\n]*[*/][^;\n]*\)',
    'bitfield': r'\b(?:u8|u16|u32|unsigned|int)\s+\w+\s*:\s*\d+',
    'hardware_address': r'\b0x(?:CC|CD|8000|C000)[0-9A-Fa-f]{4,6}\b',
    # Signatures of crashes found on device. Each is a narrower, higher-signal
    # subset of the broad rules above.
    # A static byte table read through a wider type; host endianness differs.
    'wide_read_of_symbol': r'\((?:u16|s16|u32|s32|int|f32|float)\s*\*\)\s*&\w+',
    # Struct memory indexed as a pointer array; the stride changes on 64-bit.
    'pointer_slot_index': r'\(\s*\(\s*\w+\s*\*\*\s*\)\s*\w+\s*\)\s*\[\s*\d+\s*\]',
    # Pointer slots written at a hard-coded 4-byte stride.
    'pointer_slot_stride': r'\((?:u8|char)\s*\*\)\s*[\w>.-]+\s*\+\s*\(?\s*\w+\s*(?:<<\s*2|\*\s*4)\b',
    # A pointer loaded from a fixed byte offset.
    'raw_pointer_load': r'\*\s*\(\s*[\w ]+\*\*\s*\)\s*\(\s*\w+\s*\+\s*0x',
    # User data sized for 32-bit structs.
    'fixed_size_alloc': r'\bHSD_MemAlloc\(\s*0x[0-9A-Fa-f]+\s*\)',
    # A pointer built from address arithmetic truncated to 32 bits.
    'truncated_address_load': r'\*\s*\(+\s*[\w ]+\*\*\s*\)\s*\(+\s*\(?\s*u32\s*\)|M2C_FIELD\(\s*\(u32\)',
    # A static cast to a struct that continues into the statics after it.
    'cross_static_overlay': r'=\s*\(\s*[A-Z]\w*\s*\*\s*\)\s*&?\w+_80[34][0-9A-F]{5}\s*;',
    # Bytes (strings, ids) spelled as big-endian floats; a little-endian host swaps them.
    'float_encoded_bytes': r'\b\d(?:\.\d+)?e-4[0-5]f\b',
}

NATIVE_MACROS = ('MELEE_NATIVE',)
ORIGINAL_MACROS = ('MUST_MATCH', '__MWERKS__')


def classify(directive):
    """Branch kind that a conditional directive opens, or None if unrelated."""
    words = set(re.findall(r'\w+', directive))
    negated = directive.startswith('ifndef') or '!' in directive
    if words & set(NATIVE_MACROS):
        return 'original' if negated else 'native'
    if words & set(ORIGINAL_MACROS):
        return 'native' if negated else 'original'
    return None


def branches(lines):
    """Yield the effective branch kind for every line."""
    stack = []
    for line in lines:
        directive = re.match(r'\s*#\s*(\w+)(.*)', line)
        kind = directive[1] if directive else None
        if kind in ('if', 'ifdef', 'ifndef'):
            stack.append(classify(directive[1] + directive[2]))
            yield current(stack)
        elif kind in ('elif', 'else') and stack:
            opened = stack[-1]
            flipped = {'native': 'original', 'original': 'native'}
            stack[-1] = flipped.get(opened) if kind == 'else' else classify(directive[2])
            yield current(stack)
        elif kind == 'endif' and stack:
            yield current(stack)
            stack.pop()
        else:
            yield current(stack)


def current(stack):
    for kind in reversed(stack):
        if kind is not None:
            return kind
    return 'shared'


def scan(root):
    hits = {name: [] for name in RULES}
    count = 0
    for path in sorted((root / 'src').rglob('*')):
        if path.suffix not in ('.c', '.h'):
            continue
        count += 1
        # Preserve newlines so the resulting references point to original code.
        source = re.sub(r'/\*.*?\*/', lambda m: '\n' * m[0].count('\n'),
                        path.read_text(), flags=re.S)
        lines = source.splitlines()
        for number, (line, branch) in enumerate(zip(lines, branches(lines)), 1):
            line = line.split('//', 1)[0]
            for name, pattern in RULES.items():
                if re.search(pattern, line):
                    hits[name].append({'file': str(path.relative_to(root)),
                                       'line': number, 'branch': branch,
                                       'code': line.strip()})
    return {'source_files_scanned': count,
            'scope': 'src/**/*.c and src/**/*.h, all conditional branches',
            'limitations': 'Lexical candidates only. Scalar casts and intentional disc layouts may be valid. Not a proof of absence of bugs.',
            'counts': {key: len(value) for key, value in hits.items()},
            'native_counts': {key: sum(f['branch'] != 'original' for f in value)
                              for key, value in hits.items()},
            'findings': hits}


def worklist(result, rules):
    """Markdown checklist of native-reachable findings, grouped by file."""
    by_file = {}
    for name in rules:
        for finding in result['findings'][name]:
            if finding['branch'] != 'original':
                by_file.setdefault(finding['file'], []).append((finding['line'], name, finding['code']))
    out = ['# Native portability worklist', '',
           f"Rules: {', '.join(rules)}. Branches built only for the GameCube are excluded.", '']
    for file in sorted(by_file, key=lambda f: (-len(by_file[f]), f)):
        out.append(f'## {file} ({len(by_file[file])})')
        for line, name, code in sorted(by_file[file]):
            out.append(f'- [ ] `{file}:{line}` **{name}** `{code}`')
        out.append('')
    return '\n'.join(out)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--worklist', type=Path,
                        help='also write a markdown checklist of native-reachable findings')
    parser.add_argument('--rules', default='wide_read_of_symbol,pointer_slot_index,pointer_slot_stride,raw_pointer_load,fixed_size_alloc,truncated_address_load,cross_static_overlay,float_encoded_bytes',
                        help='comma-separated rules for the worklist')
    args = parser.parse_args()
    result = scan(args.root)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    if args.worklist:
        args.worklist.write_text(worklist(result, args.rules.split(',')) + '\n')
    print(json.dumps({'files': result['source_files_scanned'],
                      'native_reachable': result['native_counts']}, indent=2))
