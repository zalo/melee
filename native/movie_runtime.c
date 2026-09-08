#include <dolphin/thp/thp.h>
#include <string.h>
#include <stdint.h>
extern int MeleeNativeDecodeVideo(const void*,void*,void*,void*);
typedef struct NativeVideoFrame {
    const void* input;
    u16 width,height;
} NativeVideoFrame;
s32 THPDec_8032F8D4(u8* data, THPDec_8032FD40_Data* out)
{
    u8 hSample[4];
    u8 vSample[4];
    u8 componentId[4];
    u8 quantizationSelector[4];
    u8 marker;
    u8 componentCount;
    u8 tag[5] = "JFIF";
    u8 i;
    u8 valid;
    u32 j;
    u16 length;

    valid = 0;
    memset(out, 0, 0xC);

    {
        u8 soi0 = *data++;
        u8 soi1 = *data++;

        if (soi0 != 0xFF || soi1 != 0xD8) {
            return 0;
        }
    }

    for (;;) {
        if (*data++ != 0xFF) {
            return 0;
        }

        while (*data == 0xFF) {
            data++;
        }
        marker = *data++;

        if (marker == 0xC0) {
            out->_pad = data[4] | (data[3] << 8);
            out->val1 = data[6] | (data[5] << 8);
            componentCount = data[7];
            data += 8;

            if (componentCount != 3) {
                return 0;
            }

            for (i = 0; i < componentCount; i++) {
                u8 factors;

                componentId[i] = *data++;
                factors = *data++;
                hSample[i] = (u8) (factors >> 4);
                vSample[i] = (u8) (factors & 0xF);
                quantizationSelector[i] = *data++;
            }

            if (hSample[0] / hSample[1] == 2 &&
                hSample[0] / hSample[2] == 2)
            {
                if (vSample[0] / vSample[1] == 2 &&
                    vSample[0] / vSample[2] == 2)
                {
                    out->val2 = 4;
                } else if (vSample[0] == vSample[1] &&
                           vSample[0] == vSample[2])
                {
                    out->val2 = 2;
                }
            } else if (hSample[0] == hSample[1] &&
                       hSample[0] == hSample[2])
            {
                if (vSample[0] == vSample[1] &&
                    vSample[0] == vSample[2])
                {
                    out->val2 = 1;
                }
            } else {
                return 0;
            }
        } else if (marker == 0xE0) {
            length = *data++;
            length = (u16) ((length << 8) | *data++);
            for (i = 0; i < 5; i++) {
                componentCount = *data++;
                if (componentCount != tag[i]) {
                    return 0;
                }
            }
            valid = 1;
            for (j = 0; j < (u32) (length - 7); j++) {
                data++;
            }
        } else if (marker == 0xDA) {
            break;
        } else if (0xC0 <= marker && marker <= 0xFE) {
            length = data[1] | (data[0] << 8);
            data += 2;

            for (j = 0; j < (u32) (length - 2); j++) {
                data++;
            }
        }

        if (out->val2 != 0 && valid != 0) {
            break;
        }
    }

    return 1;
}


s32 THPDec_8032FD40(THPDec_8032FD40_Data* data,u16 height) {
    if(data->val2!=4 || !data->val1 || !height) return 0;
    return sizeof(NativeVideoFrame);
}
intptr_t MeleeNativeTHPVideoDecode(void* header,void* status,void* work,void* input,void* metadata) {
    THPDec_8032FD40_Data parsed;
    (void)metadata;
    if(!work || !header || !input || !status) return 0;
    if(!THPDec_8032F8D4(input,&parsed) || parsed.val2!=4) { *(u8*)status=11; return 0; }
    NativeVideoFrame* frame=work;
    frame->input=input; frame->width=((u16*)header)[0]; frame->height=((u16*)header)[1];
    if(frame->width!=parsed.val1 || frame->height!=parsed._pad)
        OSPanic(__FILE__,__LINE__,"Native movie output dimensions disagree with frame");
    *(u8*)status=0;
    return (intptr_t)frame;
}
void THPDec_803313D0(intptr_t context,void* y,void* u,void* v,u32 width) {
    NativeVideoFrame* frame=(NativeVideoFrame*)context;
    if(!frame || width!=frame->width) OSPanic(__FILE__,__LINE__,"Invalid native video frame");
    int result=MeleeNativeDecodeVideo(frame->input,y,u,v);
    if(result) OSPanic(__FILE__,__LINE__,"THP frame decode failed: %d",result);
}
void THPDec_80331340(intptr_t context,void* y,void* u,void* v) {
    THPDec_803313D0(context,y,u,v,640);
}
