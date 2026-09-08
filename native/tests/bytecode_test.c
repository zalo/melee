#include <sysdolphin/baselib/bytecode.h>
#include <sysdolphin/baselib/list.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#define CHECK(v) do { if (!(v)) { fprintf(stderr,"Bytecode check failed: %s\n",#v); abort(); } } while(0)
static unsigned live_nodes;
HSD_SList* HSD_SListAllocAndPrepend(HSD_SList* prev, void* data) {
    HSD_SList* node=malloc(sizeof(*node)); CHECK(node);
    *node=(HSD_SList){prev,data}; ++live_nodes; return node;
}
HSD_SList* HSD_SListRemove(HSD_SList* node) {
    CHECK(node); HSD_SList* next=node->next; free(node); --live_nodes; return next;
}
void __assert(char* file,u32 line,char* expr) {fprintf(stderr,"%s:%u: %s\n",file,line,expr);abort();}
void HSD_Panic(char* file,u32 line,char* expr) {__assert(file,line,expr);}
void OSReport(char* fmt,...) {(void)fmt;}
int main(void) {
    float args[]={2.5f,-3.0f};
    // An odd-index argument is only four-byte aligned, even on LP64.
    u8 negate[]={2,0,1,9,1};
    CHECK(HSD_ByteCodeEval(negate,args,2)==3.0f);
    u8 arithmetic[]={2,0,0,2,0,1,0x17,6,0x40,0,0,0,0x19,1};
    CHECK(HSD_ByteCodeEval(arithmetic,args,2)==-1.0f);
    u8 integer[]={6,0,0,0,7,8,1};
    CHECK(HSD_ByteCodeEval(integer,NULL,0)==7.0f);
    u8 sine[]={6,0x42,0xB4,0,0,0x0D,1};
    CHECK(fabsf(HSD_ByteCodeEval(sine,NULL,0)-1.0f)<0.00001f);
    CHECK(live_nodes==0);
    puts("PASS: expression arguments, float stack writes, arithmetic, conversion and trigonometry");
}
