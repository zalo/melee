#!/usr/bin/env python3
"""Inventory recurring source-port hazards. Matches are candidates, not diagnoses.

Read-only: no automatic edits. Includes original-only branches deliberately so
new native implementations can be compared with their GameCube counterparts.
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
}


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
        for number, line in enumerate(source.splitlines(), 1):
            line = line.split('//', 1)[0]
            for name, pattern in RULES.items():
                if re.search(pattern, line):
                    hits[name].append({'file': str(path.relative_to(root)),
                                       'line': number, 'code': line.strip()})
    return {'source_files_scanned': count,
            'scope': 'src/**/*.c and src/**/*.h, all conditional branches',
            'limitations': 'Lexical candidates only. Scalar casts and intentional disc layouts may be valid. Not a proof of absence of bugs.',
            'counts': {key: len(value) for key, value in hits.items()}, 'findings': hits}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = scan(args.root)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'files': result['source_files_scanned'], **result['counts']}, indent=2))
