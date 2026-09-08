#include <melee_sfx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); abort(); } } while(0)
static void check_bank(const u8* data, size_t size) {
    CHECK(size >= 32);
    u32 header=MeleeSfxRead32(data), samples=MeleeSfxRead32(data+4), count=MeleeSfxRead32(data+8), base=MeleeSfxRead32(data+12);
    CHECK(header >= 16 && header <= size-16);
    CHECK(((header+16+31)&~31u)+(u64)samples <= size);
    size_t bytes=MeleeSfxGroupSize(data+16,data+32,header,count);
    CHECK(bytes);
    AXVPB* group=malloc(bytes); void* buckets[32]={0};
    CHECK(MeleeSfxBuildGroup(group,bytes,data+16,data+32,header,count,base,16384,samples,123,buckets));
    CHECK(group->priority==count && group->userContext==samples);
    u8* cursor=(u8*)group+sizeof(*group); size_t input=16;
    for (u32 i=0;i<count;++i) {
        MeleeSfxEntry* entry=(MeleeSfxEntry*)cursor;
        CHECK(entry->count==MeleeSfxRead32(data+input) && entry->id==base+i);
        CHECK(entry->rate==MeleeSfxRead32(data+input+4));
        for (int v=0;v<entry->count;++v) {
            AXPBADDR* addr=(AXPBADDR*)(cursor+offsetof(MeleeSfxEntry,addr)+v*64);
            CHECK((((u32)addr->currentAddressHi<<16)|addr->currentAddressLo) == MeleeSfxRead32(data+input+8+v*64+12)+32768);
        }
        cursor+=MeleeSfxEntrySize(entry->count); input+=8+entry->count*64;
    }
    CHECK(cursor==(u8*)group+bytes);
    CHECK(!MeleeSfxBuildGroup(group,bytes-1,data+16,data+32,header,count,base,16384,samples,123,buckets));
    free(group);
}
int main(int argc,char** argv) {
    u8 invalid[32]={0}; CHECK(!MeleeSfxGroupSize(invalid,invalid,16,1));
    for (int i=1;i<argc;++i) {
        FILE* file=fopen(argv[i],"rb"); CHECK(file); CHECK(!fseek(file,0,SEEK_END));
        long size=ftell(file); CHECK(size>0); rewind(file); u8* data=malloc(size);
        CHECK(fread(data,1,size,file)==size); fclose(file);
        printf("Checking %s\n",argv[i]); fflush(stdout); check_bank(data,size); free(data);
    }
    printf("PASS: %d original sound banks materialized with native pointers and decoded descriptors\n",argc-1);
}
