#!/usr/bin/env python3
"""Verify dependency patches cannot accidentally target the enclosing repository."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1] / 'tools/prepare_flip.py'
spec = importlib.util.spec_from_file_location('prepare_flip', SOURCE)
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


class PatchTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        self.dependency = self.root / 'build/dependency'
        self.dependency.mkdir(parents=True)
        self.source = self.dependency / 'source.txt'
        self.source.write_text('original\n')
        self.parent_source = self.root / 'source.txt'
        self.parent_source.write_text('original\n')
        self.patch = self.root / 'dependency.patch'
        self.patch.write_text(
            'diff --git a/source.txt b/source.txt\n'
            '--- a/source.txt\n+++ b/source.txt\n'
            '@@ -1 +1 @@\n-original\n+patched\n')

    def test_archive_inside_parent_repository_is_patched_idempotently(self):
        prepare.apply_patch(self.dependency, self.patch)
        self.assertEqual(self.source.read_text(), 'patched\n')
        self.assertEqual(self.parent_source.read_text(), 'original\n')
        prepare.apply_patch(self.dependency, self.patch)
        self.assertEqual(self.source.read_text(), 'patched\n')

    def test_real_dependency_checkout_is_patched_idempotently(self):
        subprocess.run(['git', 'init', '-q', str(self.dependency)], check=True)
        prepare.apply_patch(self.dependency, self.patch)
        prepare.apply_patch(self.dependency, self.patch)
        self.assertEqual(self.source.read_text(), 'patched\n')
        self.assertEqual(self.parent_source.read_text(), 'original\n')

    def test_parent_only_patch_is_rejected(self):
        (self.root / 'parent-only.txt').write_text('original\n')
        self.patch.write_text(self.patch.read_text().replace('source.txt', 'parent-only.txt'))
        with self.assertRaises(subprocess.CalledProcessError):
            prepare.apply_patch(self.dependency, self.patch)
        self.assertEqual((self.root / 'parent-only.txt').read_text(), 'original\n')
        self.assertEqual(self.source.read_text(), 'original\n')

    def test_conflicting_dependency_is_rejected(self):
        self.source.write_text('conflicting\n')
        with self.assertRaises(subprocess.CalledProcessError):
            prepare.apply_patch(self.dependency, self.patch)
        self.assertEqual(self.source.read_text(), 'conflicting\n')
        self.assertEqual(self.parent_source.read_text(), 'original\n')


if __name__ == '__main__':
    unittest.main()
