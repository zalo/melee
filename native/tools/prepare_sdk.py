"""Adapt SDK declarations to LP64 without changing the original GameCube headers."""
from pathlib import Path
import re
import sys
root = Path(__file__).resolve().parents[2]
out_root = Path(sys.argv[1])
for source in (root / 'extern/dolphin/include').rglob('*.h'):
    dest = out_root / source.relative_to(root / 'extern/dolphin/include')
    dest.parent.mkdir(parents=True, exist_ok=True)
    text = source.read_text()
    # CodeWarrior's long is 32-bit. Darwin's long is 64-bit.
    text = re.sub(r'\bunsigned long\b(?!\s+long)', 'unsigned int', text)
    text = re.sub(r'\bsigned long\b(?!\s+long)', 'signed int', text)
    text = re.sub(r'(?<!long )\blong\b(?!\s+long|\s+double)', 'int', text)
    if source.name == 'GXVert.h':
        text = (root / 'build/native-deps/aurora/include/dolphin/gx/GXVert.h').read_text()
    if source.name == 'GXStruct.h':
        text = text.replace('u32 dummy[8];', 'u32 dummy[16];')
        text = text.replace('u32 dummy[3];', 'u32 dummy[10];')
    if source.name == 'GXGeometry.h':
        text = re.sub(r'static inline void GXEnd\(void\)\n\{.*?\n\}', 'void GXEnd(void);', text, flags=re.S)
        text = text.replace('void GXSetArray(GXAttr attr, const void *base_ptr, u8 stride);',
            'void MeleeGXSetArray(GXAttr attr, const void *base_ptr, u8 stride);\n#define GXSetArray MeleeGXSetArray')
    if source.name == 'dvd.h':
        text = text.replace('DVDReadAsyncPrio(', 'MeleeNativeDVDReadAsyncPrio(')
        text += '\n#define DVDReadAsyncPrio MeleeNativeDVDReadAsyncPrio\n'
    if source.name == 'ar.h':
        text = text.replace('u32 source, u32 dest, u32 length', 'uintptr_t source, uintptr_t dest, u32 length')
        for name in ('Init', 'Reset', 'PostRequest', 'RemoveRequest', 'RemoveOwnerRequest', 'FlushQueue', 'SetChunkSize', 'GetChunkSize'):
            text = text.replace('ARQ' + name + '(', 'MeleeNativeARQ' + name + '(')
            text += '\n#define ARQ' + name + ' MeleeNativeARQ' + name + '\n'
    if source.name == 'pad.h':
        text = text.replace('PADRead(', 'MeleeNativePADRead(').replace('PADClamp(', 'MeleeNativePADClamp(')
        text += '\n#define PADRead MeleeNativePADRead\n#define PADClamp MeleeNativePADClamp\n'
    if source.name == 'card.h':
        text = text.replace('void CARDInit(void);', 'void MeleeNativeCARDInit(void);\n#define CARDInit MeleeNativeCARDInit')
        for name in ('Mount', 'Check', 'Delete', 'Rename', 'Format', 'Create', 'Read', 'Write', 'SetStatus'):
            text = re.sub(r'\bCARD' + name + r'Async\b', 'MeleeNativeCARD' + name + 'Async', text)
            text = '#define CARD' + name + 'Async MeleeNativeCARD' + name + 'Async\n' + text
    if source.name == 'thp.h':
        text = text.replace('s32 THPVideoDecode(', 'intptr_t MeleeNativeTHPVideoDecode(')
        text = text.replace('THPDec_80331340(s32,', 'THPDec_80331340(intptr_t,')
        text = text.replace('THPDec_803313D0(s32,', 'THPDec_803313D0(intptr_t,')
        text += '\n#define THPVideoDecode MeleeNativeTHPVideoDecode\n'
    if source.name == 'os.h':
        text = text.replace('#define OSRoundUp32B(x) (((u32) (x) + 32 - 1) & ~(32 - 1))', '#define OSRoundUp32B(x) (((uintptr_t)(x) + 31) & ~(uintptr_t)31)')
        text = text.replace('#define OSRoundDown32B(x) (((u32) (x)) & ~(32 - 1))', '#define OSRoundDown32B(x) ((uintptr_t)(x) & ~(uintptr_t)31)')
        text = text.replace('#define __OSBusClock (*(u32*) (OS_BASE_CACHED | 0x00F8))', '#define __OSBusClock 162000000U')
        text = text.replace('#define __OSCoreClock (*(u32*) (OS_BASE_CACHED | 0x00FC))', '#define __OSCoreClock 486000000U')
    if not dest.exists() or dest.read_text() != text:
        dest.write_text(text)
