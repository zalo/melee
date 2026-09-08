#include <dolphin/thp/thp.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"%s:%d %s\n",__FILE__,__LINE__,#x);abort();}}while(0)
static u32 word(const u8* p) {return (u32)p[0]<<24|(u32)p[1]<<16|(u32)p[2]<<8|p[3];}
void OSPanic(char* file,int line,char* format,...) {
    fprintf(stderr,"%s:%d ",file,line);va_list a;va_start(a,format);vfprintf(stderr,format,a);va_end(a);abort();
}
int main(int argc,char** argv) {
    if(argc==1) {puts("Pass an extracted MTH movie to test original frames"); return 0;}
    FILE* f=fopen(argv[1],"rb"); CHECK(f);
    u8 header[64];CHECK(fread(header,1,64,f)==64);
    u32 offset=word(header+32),size=word(header+40),frames=word(header+28);
    u16 dims[2]={word(header+16),word(header+20)};
    CHECK(dims[0] && dims[1] && frames>=3);
    u8* y=malloc(dims[0]*dims[1]),*u=malloc(dims[0]*dims[1]/4),*v=malloc(dims[0]*dims[1]/4);
    CHECK(y&&u&&v);
    unsigned decoded=0;
    for(unsigned i=0;i<3;++i) {
        u8* input=malloc(size);CHECK(input && fseek(f,offset,SEEK_SET)==0 && fread(input,1,size,f)==size);
        THPDec_8032FD40_Data metadata;
        CHECK(THPDec_8032F8D4(input+4,&metadata));
        CHECK(metadata.val1==dims[0] && metadata._pad==dims[1] && metadata.val2==4);
        void* work=malloc(THPDec_8032FD40(&metadata,dims[1]));CHECK(work);
        u32 status=0xff;
        intptr_t context=THPVideoDecode(dims,&status,work,input+4,&metadata);CHECK(context && !(status&255));
        THPDec_803313D0(context,y,u,v,dims[0]);
        unsigned sum=0;for(unsigned n=0;n<dims[0]*dims[1];++n)sum+=y[n]; CHECK(sum);
        offset+=size;size=word(input);free(work);free(input);++decoded;
    }
    free(y);free(u);free(v);fclose(f);
    printf("PASS: %u original MTH frames decoded to %ux%u YUV textures through the game ABI\n",decoded,dims[0],dims[1]);
}
