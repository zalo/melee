#!/usr/bin/env python3
"""Give the game's string literals the bytes the game expects.

The game's text routines read Shift-JIS, and the decompiled sources spell their Japanese and full-width
strings as UTF-8 text. The GameCube build converts each source file to Shift-JIS before it reaches the
compiler (sjiswrap); a native compiler would emit the UTF-8 bytes, which the text routines cannot read
(blank name plates at character select, an unusable name-entry keyboard, wrong save titles).

    sjis_literals.py --list <dir>...     print the .c files under <dir> with a non-ASCII literal
    sjis_literals.py <source> <output>   write <source> with those literals as escaped CP932 bytes

Only string and character literals change; comments keep their text. CP932 is the code page sjiswrap uses.
"""
import os
import sys


def convert(text, name):
    """Return (converted text, number of non-ASCII characters replaced inside literals)."""
    out = []
    replaced = 0
    i = 0
    n = len(text)
    quote = None  # the delimiter of the literal being read
    while i < n:
        c = text[i]
        if quote:
            if c == '\\' and i + 1 < n:
                out.append(text[i:i + 2])
                i += 2
                continue
            if c == quote or c == '\n':
                quote = None
            elif ord(c) > 0x7F:
                try:
                    data = c.encode('cp932')
                except UnicodeEncodeError:
                    line = text.count('\n', 0, i) + 1
                    raise SystemExit(f'{name}:{line}: {c!r} has no CP932 encoding')
                # Three-digit octal escapes end by themselves; a hex escape would swallow a following digit.
                out.append(''.join(f'\\{b:03o}' for b in data))
                replaced += 1
                i += 1
                continue
            out.append(c)
            i += 1
        elif text.startswith('//', i):
            end = text.find('\n', i)
            end = n if end < 0 else end
            out.append(text[i:end])
            i = end
        elif text.startswith('/*', i):
            end = text.find('*/', i + 2)
            end = n if end < 0 else end + 2
            out.append(text[i:end])
            i = end
        else:
            if c in '"\'':
                quote = c
            out.append(c)
            i += 1
    return ''.join(out), replaced


def read(path):
    with open(path, encoding='utf-8') as file:
        return file.read()


def main(argv):
    if len(argv) >= 2 and argv[0] == '--list':
        for root in argv[1:]:
            for directory, _, names in sorted(os.walk(root)):
                for name in sorted(names):
                    path = f'{directory}/{name}'
                    if name.endswith('.c') and convert(read(path), path)[1]:
                        print(path)
        return 0
    if len(argv) != 2:
        sys.stderr.write(__doc__)
        return 2
    source, output = argv
    text, _ = convert(read(source), source)
    os.makedirs(os.path.dirname(output), exist_ok=True)
    # The line marker keeps diagnostics, assert messages and debug information on the original file.
    escaped = source.replace('\\', '\\\\').replace('"', '\\"')
    with open(output, 'w', encoding='utf-8') as file:
        file.write(f'#line 1 "{escaped}"\n{text}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
