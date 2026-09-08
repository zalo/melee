#ifndef _lbarchive_h_
#define _lbarchive_h_

#include <Runtime/platform.h>

#include <sysdolphin/baselib/forward.h>

#include <sysdolphin/baselib/archive.h>

#ifdef MELEE_NATIVE
#define LB_ARCHIVE_SENTINEL __attribute__((sentinel(0, 1)))
#else
#define LB_ARCHIVE_SENTINEL
#endif

void lbArchive_InitializeDAT(HSD_Archive* archive, void* data, size_t length);
void lbArchive_LoadSections(HSD_Archive* archive, void** symbols, ...) LB_ARCHIVE_SENTINEL;
HSD_Archive* lbArchive_LoadArchive(const char* filename);
HSD_Archive* lbArchive_LoadSymbols(const char* filename, void* symbols, ...) LB_ARCHIVE_SENTINEL;
HSD_Archive* lbArchive_80016DBC(const char* filename, void* symbols, ...) LB_ARCHIVE_SENTINEL;
void lbArchive_80016EFC(HSD_Archive*);
bool lbArchive_80016F80(HSD_Archive**, const char* filename);
bool lbArchive_80017040(HSD_Archive** dst, const char* filename, void* symbols,
                        ...) LB_ARCHIVE_SENTINEL;
bool lbArchive_800171CC(HSD_Archive** dst, const char* filename, void* symbols,
                        ...) LB_ARCHIVE_SENTINEL;
int lbArchiveRelocate(HSD_Archive*, u8*, size_t file_size, intptr_t base_addr);

#endif
