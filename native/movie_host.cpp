#include <dolphin/thp.h>
extern "C" int MeleeNativeDecodeVideo(const void* input, void* y, void* u, void* v) {
    return THPVideoDecode(input,y,u,v,nullptr);
}
