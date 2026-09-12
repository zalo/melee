// End-to-end host test of Melee's save path on a 64-bit build: lbcardnew.c
// drives the HSD memory-card library (hsd_3A94.c, hsd_3B27.c) through the
// native card bridge onto an in-memory CARD backend. Pointer-sized queue slots
// and every save/load/verify path are exercised under ASan/UBSan.
#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

extern "C" {
typedef void (*CARDCallback)(int chan, int result);
struct CARDFileInfo { int chan, fileNo, offset, length; unsigned short iBlock; };
struct CARDStat {
    char fileName[32]; unsigned length, time; unsigned char gameName[4], company[2];
    unsigned char bannerFormat, padding0; unsigned iconAddr; unsigned short iconFormat, iconSpeed;
    unsigned commentAddr, offsetBanner, offsetBannerTlut, offsetIcon[8], offsetIconTlut, offsetData;
};
struct DVDDiskID { char gameName[4], company[2]; unsigned char diskNumber, gameVersion, streaming, bufSize, pad[22]; };
struct CardEntry { int file_size; int file_flags; unsigned char* data; };

// Melee entry points (src/melee/lb/lbcardnew.c)
int lb_8001BC18(int, char*, void**, void*, const char*, intptr_t, intptr_t, void*);
int lb_8001BD34(int, const char*, void*, void*);
int lb_8001BE30(int, const char*, void*, const char*, intptr_t, intptr_t, void*, void*);
unsigned lb_8001B7E0(int, char*, void*, void*, int*);
int lb_8001B6F8(void);
void lbCardNew_AllocWorkArea(void);
void lb_8001C5BC(void);
int lb_8001C4A8(void*, void*);

// Runtime stubs the game code links against.
static int interrupt_depth;
int OSDisableInterrupts(void) { return interrupt_depth++ == 0; }
int OSRestoreInterrupts(int enabled) { assert(interrupt_depth && bool(enabled) == (interrupt_depth == 1)); --interrupt_depth; return 0; }
void* HSD_MemAlloc(ssize_t size) { return aligned_alloc(32, (size + 31) & ~ssize_t(31)); }
void HSD_Free(void* ptr) { free(ptr); }
void MeleeNativeAssert(const char* expr, const char* file, int line) {
    std::fprintf(stderr, "assertion failed: %s (%s:%d)\n", expr, file, line); std::abort();
}
static DVDDiskID disk{{'G', 'A', 'L', 'E'}, {'0', '1'}, 0, 2, 0, 0, {}};
DVDDiskID* DVDGetCurrentDiskID(void) { return &disk; }

// In-memory GameCube memory card: one channel, 8 KiB sectors, 59 blocks free.
struct File { std::string name; std::vector<unsigned char> data; CARDStat stat{}; };
static struct { std::vector<File> files; unsigned reads = 0, writes = 0; bool corrupt_next_read = false; } card;
static const int kSector = 0x2000, kFreeBlocks = 59, kMaxFiles = 127;
enum { READY = 0, BUSY = -1, NOCARD = -3, NOFILE = -4, IOERROR = -5, EXIST = -7, INSSPACE = -9, FATAL = -128 };

static File* file_at(int chan, int fileNo) {
    if (chan != 0 || fileNo < 0 || size_t(fileNo) >= card.files.size()) return nullptr;
    return &card.files[fileNo];
}
int CARDProbe(int chan) { return chan == 0; }
int CARDProbeEx(int chan, int* memSize, int* sectorSize) {
    if (chan != 0) return NOCARD;
    *memSize = 64; *sectorSize = kSector; return READY;
}
int CARDMount(int chan, void*, CARDCallback) { return chan == 0 ? READY : NOCARD; }
int CARDUnmount(int chan) { return chan == 0 ? READY : NOCARD; }
int CARDCheck(int chan) { return chan == 0 ? READY : NOCARD; }
int CARDFormat(int chan) { if (chan) return NOCARD; card.files.clear(); return READY; }
int CARDGetXferredBytes(int) { return 0; }
int CARDFreeBlocks(int chan, int* bytes, int* files) {
    if (chan != 0) return NOCARD;
    int used = 0;
    for (auto& f : card.files) used += int(f.data.size() / kSector);
    *bytes = (kFreeBlocks - used) * kSector; *files = kMaxFiles - int(card.files.size()); return READY;
}
int CARDOpen(int chan, char* name, CARDFileInfo* info) {
    if (chan != 0) return NOCARD;
    for (size_t i = 0; i < card.files.size(); ++i)
        if (card.files[i].name == name) { *info = {chan, int(i), 0, int(card.files[i].data.size()), 0}; return READY; }
    return NOFILE;
}
int CARDFastOpen(int chan, int fileNo, CARDFileInfo* info) {
    auto* f = file_at(chan, fileNo);
    if (!f) return chan ? NOCARD : NOFILE;
    *info = {chan, fileNo, 0, int(f->data.size()), 0}; return READY;
}
int CARDClose(CARDFileInfo* info) { return file_at(info->chan, info->fileNo) ? READY : NOFILE; }
int CARDCreate(int chan, char* name, unsigned size, CARDFileInfo* info) {
    if (chan != 0) return NOCARD;
    assert(size % kSector == 0 && "CARDCreate size must be whole sectors");
    for (auto& f : card.files) if (f.name == name) return EXIST;
    int freeBytes, freeFiles; CARDFreeBlocks(0, &freeBytes, &freeFiles);
    if (int(size) > freeBytes) return INSSPACE;
    File f; f.name = name; f.data.assign(size, 0);
    std::strncpy(f.stat.fileName, name, sizeof f.stat.fileName);
    f.stat.length = size; std::memcpy(f.stat.gameName, "GALE", 4); std::memcpy(f.stat.company, "01", 2);
    f.stat.iconAddr = f.stat.commentAddr = 0xFFFFFFFFu;
    card.files.push_back(std::move(f));
    *info = {chan, int(card.files.size()) - 1, 0, int(size), 0}; return READY;
}
int CARDRead(CARDFileInfo* info, void* buf, int length, int offset) {
    auto* f = file_at(info->chan, info->fileNo);
    if (!f) return NOFILE;
    assert(offset >= 0 && length > 0 && size_t(offset) + size_t(length) <= f->data.size() && "read out of range");
    assert(offset % 512 == 0 && length % 512 == 0 && "hardware requires 512-byte aligned reads");
    std::memcpy(buf, f->data.data() + offset, length); ++card.reads;
    if (card.corrupt_next_read) { static_cast<unsigned char*>(buf)[length / 2] ^= 0xFF; card.corrupt_next_read = false; }
    return READY;
}
int CARDWrite(CARDFileInfo* info, void* buf, int length, int offset) {
    auto* f = file_at(info->chan, info->fileNo);
    if (!f) return NOFILE;
    assert(offset >= 0 && length > 0 && size_t(offset) + size_t(length) <= f->data.size() && "write out of range");
    assert(offset % kSector == 0 && length % kSector == 0 && "hardware writes whole sectors");
    std::memcpy(f->data.data() + offset, buf, length); ++card.writes; return READY;
}
int CARDGetStatus(int chan, int fileNo, CARDStat* stat) {
    auto* f = file_at(chan, fileNo);
    if (!f) return chan ? NOCARD : NOFILE;
    *stat = f->stat; return READY;
}
int CARDSetStatus(int chan, int fileNo, CARDStat* stat) {
    auto* f = file_at(chan, fileNo);
    if (!f) return chan ? NOCARD : NOFILE;
    f->stat.bannerFormat = stat->bannerFormat; f->stat.iconAddr = stat->iconAddr; f->stat.iconFormat = stat->iconFormat;
    f->stat.iconSpeed = stat->iconSpeed; f->stat.commentAddr = stat->commentAddr; return READY;
}
int CARDDelete(int chan, char* name) {
    if (chan != 0) return NOCARD;
    for (size_t i = 0; i < card.files.size(); ++i)
        if (card.files[i].name == name) { card.files.erase(card.files.begin() + i); return READY; }
    return NOFILE;
}
int CARDRename(int chan, char* from, char* to) {
    if (chan != 0) return NOCARD;
    for (auto& f : card.files) if (f.name == from) { f.name = to; std::strncpy(f.stat.fileName, to, 32); return READY; }
    return NOFILE;
}
}

// Melee's save manifest (lbcardgame.c): sub-file 1 is the main record, 2-8 are
// snapshot slots; sub-file 0 carries the icon block bookkeeping.
static const int kSizes[9] = {0, 0x1790, 0x1F2C, 0x1F2C, 0x1F2C, 0x1F2C, 0x1F2C, 0x1F2C, 0x1F2C};
static const int kFlags[9] = {3, 0, 1, 1, 1, 1, 1, 1, 1};

static void fill(std::vector<unsigned char>& v, unsigned seed) {
    unsigned x = seed * 2654435761u + 1;
    for (auto& b : v) { x = x * 1103515245u + 12345u; b = (unsigned char) (x >> 16); }
}
static int finish(int result) { while (result == 0xB) result = lb_8001B6F8(); return result; }

int main() {
    alarm(60);  // a stalled card queue must fail the test, not hang it
    char name[] = "SuperSmashBros0110290334";
    char comment[64] = "Super Smash Bros. Melee         Game Data 2026/09/11";
    // Byte image of lbcardgame.c's icon manifest: CI8 banner, one CI8 icon.
    unsigned char icon_header[20] = {2, 0, 1, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    std::vector<unsigned char> banner(0x1800), icons(0x600);
    fill(banner, 7); fill(icons, 8);
    std::vector<std::vector<unsigned char>> payload(9), expected(9);
    CardEntry entries[10]{};
    for (int i = 0; i < 9; ++i) {
        payload[i].assign(kSizes[i], 0); expected[i].assign(kSizes[i], 0);
        fill(expected[i], 100 + i); payload[i] = expected[i];
        entries[i] = {kSizes[i], kFlags[i], kSizes[i] ? payload[i].data() : nullptr};
    }
    entries[9] = {-1, 0, nullptr};

    lbCardNew_AllocWorkArea();
    lb_8001C5BC();
    int status = 0;
    // 1. Fresh card: the load reports "no save file" (4) without touching data.
    unsigned probe = lb_8001B7E0(0, name, entries, icon_header, &status);
    std::printf("load on empty card -> %u (expect 4)\n", probe);
    assert(probe == 4 && card.files.empty());

    // 2. Create the save. This is the path behind the boot prompt's "Yes".
    int blocks = lb_8001C4A8(entries, icon_header);
    int r = lb_8001BC18(0, name, (void**) entries, icon_header, comment, (intptr_t) banner.data(), (intptr_t) icons.data(), &status);
    std::printf("create -> %d, %d blocks, %zu file(s), %u sector writes\n", r, blocks, card.files.size(), card.writes);
    assert(r == 0 && card.files.size() == 1);
    File& saved = card.files[0];
    assert(saved.name == name && int(saved.data.size()) == blocks * kSector);
    assert((saved.stat.bannerFormat & 3) == 2 && saved.stat.iconAddr == 0x40 && saved.stat.commentAddr == 0);
    assert(saved.stat.iconFormat == 1 && saved.stat.iconSpeed == 3);
    assert(std::memcmp(saved.data.data(), comment, 64) == 0 && "comment leads the first block");
    assert(std::memcmp(saved.data.data() + 0x40, banner.data(), banner.size()) == 0 && "banner follows the comment");
    assert(std::memcmp(saved.data.data() + 0x1840, icons.data(), icons.size()) == 0 && "icons follow the banner");

    // 3. Boot again: the save is found and its directory verified. The probe
    //    reports 1 (file present) rather than 4 (absent); gm_1AED.c treats
    //    both 0 and 1 as "no prompt needed".
    lb_8001C5BC();
    probe = lb_8001B7E0(0, name, entries, icon_header, &status);
    std::printf("load with save -> %u (expect 1: present)\n", probe);
    assert(probe == 1);

    // 4. Read every sub-file back into cleared buffers.
    for (auto& p : payload) std::fill(p.begin(), p.end(), 0);
    r = finish(lb_8001BD34(0, name, entries, &status));
    std::printf("read all -> %d\n", r);
    assert(r == 0);
    for (int i = 1; i < 9; ++i) assert(payload[i] == expected[i] && "sub-file round trip");

    // 5. Rewrite with new contents (post-match autosave) and read back.
    for (int i = 1; i < 9; ++i) { fill(expected[i], 200 + i); payload[i] = expected[i]; }
    unsigned writes_before = card.writes;
    r = finish(lb_8001BE30(0, name, entries, comment, (intptr_t) banner.data(), (intptr_t) icons.data(), &status, nullptr));
    std::printf("write all -> %d, %u sector writes\n", r, card.writes - writes_before);
    assert(r == 0 && card.writes > writes_before);
    for (auto& p : payload) std::fill(p.begin(), p.end(), 0);
    r = finish(lb_8001BD34(0, name, entries, &status));
    assert(r == 0);
    for (int i = 1; i < 9; ++i) assert(payload[i] == expected[i] && "rewritten sub-file round trip");

    // 6. A corrupted sector is detected by the digest check instead of being
    //    handed to the game as valid data.
    lb_8001C5BC();
    probe = lb_8001B7E0(0, name, entries, icon_header, &status);
    assert(probe == 1);
    card.corrupt_next_read = true;
    for (auto& p : payload) std::fill(p.begin(), p.end(), 0);
    r = finish(lb_8001BD34(0, name, entries, &status));
    bool intact = true;
    for (int i = 1; i < 9; ++i) intact = intact && payload[i] == expected[i];
    std::printf("read with one corrupted sector -> %d, data %s\n", r, intact ? "recovered" : "rejected");
    assert((r != 0 || intact) && "corruption must be reported or repaired, never returned silently");

    std::printf("PASS: create, reload, read, rewrite and corruption detection through the 64-bit card queue "
                "(%u reads, %u writes, %zu-byte save)\n", card.reads, card.writes, saved.data.size());
    return 0;
}
