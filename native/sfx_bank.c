#include <melee_sfx.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
u32 MeleeSfxRead32(const void* data) { const u8* p=data; return (u32)p[0]<<24 | (u32)p[1]<<16 | (u32)p[2]<<8 | p[3]; }
static u8 byte_at(const u8* first, const u8* rest, size_t offset) { return offset < 16 ? first[offset] : rest[offset-16]; }
static u16 read16(const u8* a, const u8* b, size_t at) { return (u16)byte_at(a,b,at)<<8 | byte_at(a,b,at+1); }
static u32 read32(const u8* a, const u8* b, size_t at) { return (u32)read16(a,b,at)<<16 | read16(a,b,at+2); }
size_t MeleeSfxEntrySize(u32 count) { return (offsetof(MeleeSfxEntry,addr) + (size_t)count*64 + 7) & ~(size_t)7; }
size_t MeleeSfxGroupSize(const u8* a, const u8* b, size_t size, u32 count) {
    size_t offset=0, output=sizeof(AXVPB);
    if (!a || (!b && size>16) || count>size/8) return 0;
    for (u32 i=0;i<count;++i) {
        if (offset>size || size-offset<8) return 0;
        u32 voices=read32(a,b,offset);
        if (voices<1 || voices>2 || size-offset<8+voices*64) return 0;
        offset+=8+voices*64; output+=MeleeSfxEntrySize(voices);
    }
    return output;
}
int MeleeSfxBuildGroup(AXVPB* group, size_t capacity, const u8* a, const u8* b,
    size_t size, u32 count, u32 base, u32 aram, u32 aram_size, int entry, void** buckets) {
    size_t required=MeleeSfxGroupSize(a,b,size,count);
    if (!required || capacity<required || aram>UINT32_MAX/2 || base>INT_MAX || count>INT_MAX-base) return 0;
    // Validate all sample addresses before publishing anything into lookup buckets.
    size_t offset=0;
    for(u32 i=0;i<count;++i) {
        u32 voices=read32(a,b,offset);
        for(u32 v=0;v<voices;++v) {
            size_t at=offset+8+v*64;
            for(unsigned address=4;address<16;address+=4) {
                u32 value=read32(a,b,at+address);
                if ((u64)value >= (u64)aram_size*2 || (u64)value + (u64)aram*2 > UINT32_MAX) return 0;
            }
        }
        offset+=8+voices*64;
    }
    memset(group,0,required);
    group->prev=(AXVPB*)(intptr_t)entry;
    group->next1=(void*)(uintptr_t)base; group->priority=count;
    group->callback=(void (*)(void*))(uintptr_t)aram; group->userContext=aram_size;
    u8* output=(u8*)group+sizeof(*group); offset=0;
    for(u32 i=0;i<count;++i) {
        u32 voices=read32(a,b,offset);
        MeleeSfxEntry* sound=(MeleeSfxEntry*)output;
        sound->id=base+i; sound->count=voices; sound->rate=read32(a,b,offset+4);
        for(u32 v=0;v<voices;++v) {
            size_t at=offset+8+v*64;
            u16* dst=(u16*)(output+offsetof(MeleeSfxEntry,addr)+v*64);
            for(unsigned h=0;h<32;++h) dst[h]=read16(a,b,at+h*2);
            AXPBADDR* addr=(AXPBADDR*)dst;
            // Address pairs have host field order, not the host u32 byte order.
            u32 loop=((u32)addr->loopAddressHi<<16)|addr->loopAddressLo;
            u32 end=((u32)addr->endAddressHi<<16)|addr->endAddressLo;
            u32 current=((u32)addr->currentAddressHi<<16)|addr->currentAddressLo;
            loop+=aram*2; end+=aram*2; current+=aram*2;
            addr->loopAddressHi=loop>>16; addr->loopAddressLo=loop;
            addr->endAddressHi=end>>16; addr->endAddressLo=end;
            addr->currentAddressHi=current>>16; addr->currentAddressLo=current;
        }
        unsigned bucket=sound->id&31; sound->next=buckets[bucket]; buckets[bucket]=sound;
        output+=MeleeSfxEntrySize(voices); offset+=8+voices*64;
    }
    return 1;
}
