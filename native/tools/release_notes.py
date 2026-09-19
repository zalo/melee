#!/usr/bin/env python3
"""Render the GitHub release notes for a PortMaster zip.

    release_notes.py --zip dist/portmaster/melee.zip --tag portmaster-20260919-83990e2 --commit <sha> [--date 2026-09-19]
                     [--template native/platform/portmaster/RELEASE_NOTES.md]

Fills {{TAG}}, {{COMMIT}}, {{SHORT}} (nine characters of it), {{DATE}}, {{SIZE}} and {{SHA256}} in the template and prints the result; fails
when a placeholder the template uses is unknown or one stays unfilled.
"""
import argparse
import datetime
import hashlib
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
TEMPLATE = ROOT / 'native/platform/portmaster/RELEASE_NOTES.md'


def render(template: str, zip_path: pathlib.Path, tag: str, commit: str, date: str) -> str:
    data = zip_path.read_bytes()
    values = {
        'TAG': tag,
        'COMMIT': commit,
        'SHORT': commit[:9],
        'DATE': date,
        'SIZE': f'{len(data):,}',
        'SHA256': hashlib.sha256(data).hexdigest(),
    }
    unknown = sorted(set(re.findall(r'\{\{([A-Z0-9_]+)\}\}', template)) - set(values))
    if unknown:
        raise SystemExit(f'release_notes: template uses unknown placeholders {unknown}')
    text = template
    for key, value in values.items():
        text = text.replace('{{' + key + '}}', value)
    if '{{' in text:
        raise SystemExit('release_notes: unfilled placeholder left in the notes')
    return text


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--zip', type=pathlib.Path, required=True)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--commit', required=True, help='full commit SHA the release was built from')
    parser.add_argument('--date', default=datetime.date.today().isoformat())
    parser.add_argument('--template', type=pathlib.Path, default=TEMPLATE)
    args = parser.parse_args()
    sys.stdout.write(render(args.template.read_text(), args.zip, args.tag, args.commit, args.date))


if __name__ == '__main__':
    main()
