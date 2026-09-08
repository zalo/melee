#include <sysdolphin/baselib/archive.h>
#include <melee_archive.h>
#include <melee_sfx.h>
#include <string.h>
s32 MeleeArchiveParse(HSD_Archive* archive,u8* data,size_t size) {
    if (!archive || !data || MeleeNativeArchiveCreate(archive,data,size)) return -1;
    memset(archive,0,sizeof(*archive));
    u32* header=(u32*)&archive->header;
    for(unsigned i=0;i<5;++i) header[i]=MeleeSfxRead32(data+i*4);
    memcpy(archive->header.version,data+20,4);
    archive->flags=HSD_ARCHIVE_DONT_FREE; archive->top_ptr=data; archive->data=data+32;
    size_t offset=32+archive->header.data_size;
    size_t counts[3]={archive->header.nb_reloc,archive->header.nb_public,archive->header.nb_extern};
    void* tables[3]={0};
    for(unsigned t=0;t<3;++t) {
        size_t words=counts[t]*(t?2:1);
        if(words) {
            u32* output=MeleeNativeArchiveAllocate(archive,words*4);
            for(size_t i=0;i<words;++i) output[i]=MeleeSfxRead32(data+offset+i*4);
            tables[t]=output;
        }
        offset+=words*4;
    }
    archive->reloc_info=tables[0]; archive->public_info=tables[1]; archive->extern_info=tables[2];
    archive->symbols=(char*)data+offset;
    return 0;
}
