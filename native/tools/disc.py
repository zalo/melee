"""Read GameCube ISO/CISO files without an emulator or platform-sized pointers."""
from pathlib import Path, PurePosixPath
import hashlib
import struct

class Disc:
    def __init__(self, path):
        self.path = Path(path)
        self.file = self.path.open('rb')
        try:
            self._initialize()
        except BaseException:
            self.file.close()
            raise

    def _initialize(self):
        self.length = self.path.stat().st_size
        self.block_size = 0
        self.mapping = []
        if self.file.read(4) == b'CISO':
            self.file.seek(0)
            header = self.file.read(0x8000)
            if len(header) != 0x8000:
                raise ValueError('Truncated CISO header')
            self.block_size = struct.unpack_from('<I', header, 4)[0]
            if not 0 < self.block_size <= 0x10000000:
                raise ValueError('Invalid CISO block size')
            present = 0
            for flag in header[8:]:
                if flag not in (0, 1):
                    raise ValueError('Invalid CISO block map')
                self.mapping.append(present if flag else None)
                present += flag
            if self.length < 0x8000 + present * self.block_size:
                raise ValueError('Truncated or inconsistent CISO data')
        header = self.read(0, 0x440)
        if header[:8] != b'GALE01\x00\x02':
            raise ValueError('Expected Melee US 1.02 (GALE01, revision 2)')
        dol_offset, fst_offset, fst_size = struct.unpack_from('>III', header, 0x420)
        dol_header = self.read(dol_offset, 0x100)
        offsets = struct.unpack_from('>18I', dol_header, 0)
        sizes = struct.unpack_from('>18I', dol_header, 0x90)
        dol_size = max([0x100] + [o+s for o,s in zip(offsets,sizes) if s])
        if hashlib.sha1(self.read(dol_offset, dol_size)).hexdigest() != '08e0bf20134dfcb260699671004527b2d6bb1a45':
            raise ValueError('Executable checksum does not match Melee US 1.02')
        self.fst = self.read(fst_offset, fst_size)
        self.files = self._files()

    def close(self): self.file.close()
    def __enter__(self): return self
    def __exit__(self, *args): self.close()

    def read(self, offset, size):
        if offset < 0 or size < 0:
            raise ValueError('Negative disc range')
        limit = len(self.mapping)*self.block_size if self.block_size else self.length
        if offset+size > limit:
            raise ValueError('Disc range out of bounds')
        result = bytearray()
        while size:
            if self.block_size:
                block, position = divmod(offset, self.block_size)
                count = min(size, self.block_size-position)
                physical = self.mapping[block]
                if physical is None:
                    result.extend(bytes(count))
                    offset += count; size -= count
                    continue
                address = 0x8000+physical*self.block_size+position
            else:
                address, count = offset, size
            self.file.seek(address)
            data = self.file.read(count)
            if len(data) != count:
                raise ValueError('Truncated disc read')
            result.extend(data)
            offset += count; size -= count
        return bytes(result)

    def _files(self):
        if len(self.fst) < 12:
            raise ValueError('Missing filesystem root')
        count = struct.unpack_from('>I', self.fst, 8)[0]
        if count < 1 or count*12 > len(self.fst):
            raise ValueError('Invalid filesystem entry count')
        if struct.unpack_from('>II', self.fst, 0) != (0x01000000, 0):
            raise ValueError('Invalid filesystem root')
        strings = self.fst[count*12:]
        stack = [(count, PurePosixPath(), 0)]
        files = {}
        paths = set()
        for index in range(1,count):
            while stack and index >= stack[-1][0]: stack.pop()
            if not stack: raise ValueError('Invalid directory nesting')
            kind_name, offset, size = struct.unpack_from('>III',self.fst,index*12)
            string_offset = kind_name & 0xffffff
            end = strings.find(b'\0',string_offset)
            if end < 0: raise ValueError('Unterminated filename')
            name = strings[string_offset:end].decode('ascii')
            if not name or name in ('.','..') or '/' in name or '\\' in name:
                raise ValueError('Unsafe filename')
            path = stack[-1][1]/name
            if str(path) in paths: raise ValueError('Duplicate filesystem path')
            paths.add(str(path))
            kind = kind_name >> 24
            if kind not in (0, 1): raise ValueError('Invalid filesystem entry type')
            if kind == 1:
                if offset != stack[-1][2]: raise ValueError('Invalid directory parent')
                if not index < size <= stack[-1][0]: raise ValueError('Invalid directory end')
                stack.append((size,path,index))
            else:
                if str(path) in files: raise ValueError('Duplicate filename')
                files[str(path)] = (offset,size)
        return files

    def read_file(self, name): return self.read(*self.files[name])
