#include <dolphin/gx.h>
#include <Runtime/gx_port.h>
_Static_assert(sizeof(GXTexObj) == 64, "Aurora native texture ABI");
_Static_assert(sizeof(GXTlutObj) == 40, "Aurora native palette ABI");
void MeleeNativeEmitQuad(void) {
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    HSD_FIFO_F32(-0.8f); HSD_FIFO_F32(0.8f); HSD_FIFO_F32(-1.0f);
    GXTexCoord2f32(0, 0);
    GXPosition3f32(0.8f, 0.8f, -1); GXTexCoord2f32(1, 0);
    GXPosition3f32(0.8f, -0.8f, -1); GXTexCoord2f32(1, 1);
    GXPosition3f32(-0.8f, -0.8f, -1); GXTexCoord2f32(0, 1);
    GXEnd();
}
