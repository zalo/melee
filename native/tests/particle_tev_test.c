#include <sysdolphin/baselib/psdisptev.h>
#include <sysdolphin/baselib/psstructs.h>
#include <dolphin/gx.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
static unsigned texgens, stages;
static int texture_alpha;
void GXSetNumTexGens(u8 n) { texgens=n; }
void GXSetNumTevStages(u8 n) { stages=n; }
void GXSetTevAlphaIn(GXTevStageID s,GXTevAlphaArg a,GXTevAlphaArg b,GXTevAlphaArg c,GXTevAlphaArg d) {
    texture_alpha |= a==GX_CA_TEXA || b==GX_CA_TEXA || c==GX_CA_TEXA || d==GX_CA_TEXA;
}
void GXSetTevColorIn(GXTevStageID s,GXTevColorArg a,GXTevColorArg b,GXTevColorArg c,GXTevColorArg d) {}
void GXSetTevOrder(GXTevStageID s,GXTexCoordID c,GXTexMapID m,GXChannelID col) {}
void GXSetTevColorOp(GXTevStageID s,GXTevOp o,GXTevBias b,GXTevScale sc,GXBool cl,GXTevRegID r) {}
void GXSetTevAlphaOp(GXTevStageID s,GXTevOp o,GXTevBias b,GXTevScale sc,GXBool cl,GXTevRegID r) {}
void GXSetTevSwapMode(GXTevStageID s,GXTevSwapSel r,GXTevSwapSel t) {}
void GXSetTevOp(GXTevStageID s,GXTevMode m) { if (m==GX_MODULATE) texture_alpha=1; }
int main(void) {
    /* A live list link must neither select material flags nor be overwritten. */
    HSD_Particle p={0};
    p.next=(HSD_Particle*)(uintptr_t)0x100000080ULL;
    for (unsigned bits=0;bits<16;++bits) {
        p.kind=((bits&1)?0x400:0)|((bits&2)?0x80:0)|((bits&4)?0x100000:0)|((bits&8)?0x80000000:0);
        texture_alpha=0;texgens=99;stages=0;
        psSetupTevInvalidState();psSetupTev((u32*)&p);
        assert(texgens==((bits&1)?1:0));
        assert(texture_alpha==((bits&1)?1:0));
        assert(stages>=1 && stages<=3);
        assert(p.next==(HSD_Particle*)(uintptr_t)0x100000080ULL);
        if (!(bits&1)) assert(!(p.kind&0x80));
    }
    puts("PASS: particle materials retain texture alpha and preserve 64-bit list links");
}
