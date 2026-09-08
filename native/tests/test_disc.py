import io
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from disc import Disc

class DiscTests(unittest.TestCase):
    def test_sparse_block_boundary(self):
        reader = Disc.__new__(Disc)
        reader.file = io.BytesIO(bytes(0x8000) + b'abcdWXYZ')
        reader.length = 0x8008
        reader.block_size = 4
        reader.mapping = [0, None, 1]
        self.assertEqual(reader.read(2, 8), b'cd\0\0\0\0WX')
        self.assertEqual(reader.read(12, 0), b'')
        for offset, size in [(-1, 0), (0, -1), (12, 1)]:
            with self.assertRaises(ValueError): reader.read(offset, size)

    def test_nested_filesystem(self):
        reader = Disc.__new__(Disc)
        entries = [(0x1000000, 0, 4), (0x1000000, 0, 3), (4, 100, 20), (6, 200, 30)]
        reader.fst = b''.join(struct.pack('>III', *e) for e in entries) + b'dir\0a\0b\0'
        self.assertEqual(reader._files(), {'dir/a': (100, 20), 'b': (200, 30)})
        original = reader.fst
        for field, value in [(16, 2), (20, 5), (24, 0xffffff), (24, 0x2000004)]:
            data = bytearray(original)
            struct.pack_into('>I', data, field, value)
            reader.fst = bytes(data)
            with self.assertRaises(ValueError): reader._files()

    def test_path_traversal(self):
        reader = Disc.__new__(Disc)
        for name in (b'..', b'.', b'', b'a/b', b'a\\b'):
            reader.fst = struct.pack('>IIIIII', 0x1000000, 0, 2, 0, 100, 1) + name + b'\0'
            with self.assertRaises(ValueError): reader._files()

if __name__ == '__main__': unittest.main()
