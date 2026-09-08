#ifndef RUNTIME_GX_PORT_H
#define RUNTIME_GX_PORT_H
/* Preserve console FIFO stores; the native build emits through Aurora's API. */
#ifdef MELEE_NATIVE
void MeleeNativeSetArrayData(int attribute, const void* data, unsigned int size,
                            unsigned char stride, int little_endian);
#define HSD_GX_SET_ARRAY(attr, data, stride) \
    MeleeNativeSetArrayData((attr), (data), sizeof(data), (stride), 1)
#define HSD_FIFO_F32(value) GXParam1f32(value)
#define HSD_FIFO_U8(value) GXParam1u8(value)
#else
#define HSD_GX_SET_ARRAY(attr, data, stride) GXSetArray((attr), (data), (stride))
#define HSD_FIFO_F32(value) (GXWGFifo.f32 = (value))
#define HSD_FIFO_U8(value) (GXWGFifo.u8 = (value))
#endif
#endif
