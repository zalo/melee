#include <dolphin/axfx.h>
#include <dolphin/os.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>

void* AXFXAllocFunction(unsigned int size) { return malloc(size); }
void AXFXFreeFunction(void* ptr) { free(ptr); }
void* (*__AXFXAlloc)(unsigned int) = AXFXAllocFunction;
void (*__AXFXFree)(void*) = AXFXFreeFunction;
void AXFXSetHooks(void* (*alloc_hook)(unsigned int), void (*free_hook)(void*)) {
    if (!alloc_hook || !free_hook) OSPanic(__FILE__, __LINE__, "Invalid AXFX allocator");
    int enabled = OSDisableInterrupts();
    __AXFXAlloc = alloc_hook; __AXFXFree = free_hook;
    OSRestoreInterrupts(enabled);
}
static s32 clip32(s64 value) { return value < INT_MIN ? INT_MIN : value > INT_MAX ? INT_MAX : value; }
static s32 float_to_sample(float value) { return value <= (float)INT_MIN ? INT_MIN : value >= (float)INT_MAX ? INT_MAX : (s32)value; }
int AXFXDelayShutdown(struct AXFX_DELAY* delay) {
    int enabled = OSDisableInterrupts();
    if (delay->left) __AXFXFree(delay->left);
    if (delay->right) __AXFXFree(delay->right);
    if (delay->sur) __AXFXFree(delay->sur);
    delay->left = delay->right = delay->sur = NULL;
    OSRestoreInterrupts(enabled); return 1;
}
int AXFXDelaySettings(struct AXFX_DELAY* delay) {
    for (unsigned c = 0; c < 3; ++c)
        if (delay->delay[c] <= 5 || delay->delay[c] > 5000 || delay->feedback[c] > 100 || delay->output[c] > 100) return 0;
    int enabled = OSDisableInterrupts();
    AXFXDelayShutdown(delay);
    s32** buffers[3] = {&delay->left, &delay->right, &delay->sur};
    for (unsigned c = 0; c < 3; ++c) {
        delay->currentSize[c] = ((delay->delay[c] - 5) * 32 + 159) / 160;
        delay->currentPos[c] = 0;
        delay->currentFeedback[c] = delay->feedback[c] * 128 / 100;
        delay->currentOutput[c] = delay->output[c] * 128 / 100;
        unsigned bytes = delay->currentSize[c] * 160 * sizeof(s32);
        *buffers[c] = __AXFXAlloc(bytes);
        if (!*buffers[c]) { AXFXDelayShutdown(delay); OSRestoreInterrupts(enabled); return 0; }
        memset(*buffers[c], 0, bytes);
    }
    OSRestoreInterrupts(enabled); return 1;
}
int AXFXDelayInit(struct AXFX_DELAY* delay) {
    delay->left = delay->right = delay->sur = NULL;
    return AXFXDelaySettings(delay);
}
void AXFXDelayCallback(struct AXFX_BUFFERUPDATE* update, struct AXFX_DELAY* delay) {
    s32* channels[3] = {update->left, update->right, update->surround};
    s32* history[3] = {delay->left, delay->right, delay->sur};
    for (unsigned c = 0; c < 3; ++c) {
        if (!history[c] || !delay->currentSize[c]) OSPanic(__FILE__, __LINE__, "Uninitialized AX delay");
        s32* block = history[c] + delay->currentPos[c] * 160;
        for (unsigned n = 0; n < 160; ++n) {
            s32 old = block[n];
            block[n] = clip32((s64)channels[c][n] + (((s64)old * delay->currentFeedback[c]) >> 7));
            channels[c][n] = clip32(((s64)old * delay->currentOutput[c]) >> 7);
        }
        delay->currentPos[c] = (delay->currentPos[c] + 1) % delay->currentSize[c];
    }
}
static int line_create(struct AXFX_REVSTD_DELAYLINE* line, int lag) {
    line->length = (lag + 2) * 4;
    line->inPoint = 0; line->outPoint = 8; line->lastOutput = 0;
    line->inputs = __AXFXAlloc(line->length);
    if (!line->inputs) return 0;
    memset(line->inputs, 0, line->length); return 1;
}
static float line_push(struct AXFX_REVSTD_DELAYLINE* line, float value) {
    line->inputs[line->inPoint / 4] = value;
    float output = line->inputs[line->outPoint / 4];
    line->inPoint = (line->inPoint + 4) % line->length;
    line->outPoint = (line->outPoint + 4) % line->length;
    line->lastOutput = output; return output;
}
int AXFXReverbStdShutdown(struct AXFX_REVERBSTD* reverb) {
    int enabled = OSDisableInterrupts();
    for (unsigned i = 0; i < 6; ++i) {
        if (reverb->rv.C[i].inputs) __AXFXFree(reverb->rv.C[i].inputs);
        if (reverb->rv.AP[i].inputs) __AXFXFree(reverb->rv.AP[i].inputs);
    }
    for (unsigned i = 0; i < 3; ++i)
        if (reverb->rv.preDelayLine[i]) __AXFXFree(reverb->rv.preDelayLine[i]);
    memset(&reverb->rv, 0, sizeof(reverb->rv));
    reverb->tempDisableFX = 1;
    OSRestoreInterrupts(enabled); return 1;
}
int AXFXReverbStdInit(struct AXFX_REVERBSTD* reverb) {
    if (!isfinite(reverb->time) || reverb->time < .01f || reverb->time > 10 ||
        !isfinite(reverb->coloration) || reverb->coloration < 0 || reverb->coloration > 1 ||
        !isfinite(reverb->mix) || reverb->mix < 0 || reverb->mix > 1 ||
        !isfinite(reverb->damping) || reverb->damping < 0 || reverb->damping > 1 ||
        !isfinite(reverb->preDelay) || reverb->preDelay < 0 || reverb->preDelay > .1f) return 0;
    int enabled = OSDisableInterrupts();
    struct AXFX_REVSTD_WORK* rv = &reverb->rv;
    memset(rv, 0, sizeof(*rv));
    const int comb[2] = {1789, 1999}, allpass[2] = {433, 149};
    for (unsigned c = 0; c < 3; ++c) {
        for (unsigned i = 0; i < 2; ++i) {
            unsigned index = c * 2 + i;
            if (!line_create(&rv->C[index], comb[i]) || !line_create(&rv->AP[index], allpass[i])) goto failure;
            rv->combCoef[index] = powf(10, -3.0f * comb[i] / (32000 * reverb->time));
        }
    }
    rv->allPassCoeff = reverb->coloration; rv->level = reverb->mix;
    rv->damping = 1 - (.05f + .8f * fmaxf(.05f, reverb->damping));
    rv->preDelayTime = 32000 * reverb->preDelay;
    if (rv->preDelayTime) {
        for (unsigned c = 0; c < 3; ++c) {
            rv->preDelayLine[c] = __AXFXAlloc(rv->preDelayTime * sizeof(float));
            if (!rv->preDelayLine[c]) goto failure;
            memset(rv->preDelayLine[c], 0, rv->preDelayTime * sizeof(float));
            rv->preDelayPtr[c] = rv->preDelayLine[c];
        }
    }
    reverb->tempDisableFX = 0;
    OSRestoreInterrupts(enabled); return 1;
failure:
    AXFXReverbStdShutdown(reverb); OSRestoreInterrupts(enabled); return 0;
}
int AXFXReverbStdSettings(struct AXFX_REVERBSTD* reverb) {
    int enabled = OSDisableInterrupts();
    AXFXReverbStdShutdown(reverb); int result = AXFXReverbStdInit(reverb);
    OSRestoreInterrupts(enabled); return result;
}
void AXFXReverbStdCallback(struct AXFX_BUFFERUPDATE* update, struct AXFX_REVERBSTD* reverb) {
    if (reverb->tempDisableFX) return;
    struct AXFX_REVSTD_WORK* rv = &reverb->rv;
    s32* channels[3] = {update->left, update->right, update->surround};
    float wet = .6f * rv->level, dry = .6f - wet;
    // Scalar translation of the SDK's paired comb, all-pass, damping, all-pass chain.
    for (unsigned c = 0; c < 3; ++c) {
        for (unsigned n = 0; n < 160; ++n) {
            float original = channels[c][n], input = original;
            if (rv->preDelayTime) {
                input = *rv->preDelayPtr[c]; *rv->preDelayPtr[c]++ = original;
                if (rv->preDelayPtr[c] == rv->preDelayLine[c] + rv->preDelayTime) rv->preDelayPtr[c] = rv->preDelayLine[c];
            }
            float summed = 0;
            for (unsigned i = 0; i < 2; ++i) {
                struct AXFX_REVSTD_DELAYLINE* line = &rv->C[c*2+i];
                summed += line_push(line, fmaf(rv->combCoef[c*2+i], line->lastOutput, input));
            }
            struct AXFX_REVSTD_DELAYLINE* a = &rv->AP[c*2];
            float next = fmaf(rv->allPassCoeff, a->lastOutput, summed);
            float signal = fmaf(-rv->allPassCoeff, next, a->lastOutput); line_push(a, next);
            signal = fmaf(rv->damping, rv->lpLastout[c], .3f * signal); rv->lpLastout[c] = signal;
            a = &rv->AP[c*2+1]; next = fmaf(rv->allPassCoeff, a->lastOutput, signal);
            signal = fmaf(-rv->allPassCoeff, next, a->lastOutput); line_push(a, next);
            channels[c][n] = float_to_sample(fmaf(wet, signal, dry * original));
        }
    }
}

static int hi_line_create(struct AXFX_REVHI_DELAYLINE* line, int lag) {
    line->length = (lag + 2) * 4;
    line->inPoint = 0; line->outPoint = 8; line->lastOutput = 0;
    line->inputs = __AXFXAlloc(line->length);
    if (!line->inputs) return 0;
    memset(line->inputs, 0, line->length); return 1;
}
static float hi_line_push(struct AXFX_REVHI_DELAYLINE* line, float value) {
    line->inputs[line->inPoint / 4] = value;
    float output = line->inputs[line->outPoint / 4];
    line->inPoint = (line->inPoint + 4) % line->length;
    line->outPoint = (line->outPoint + 4) % line->length;
    line->lastOutput = output; return output;
}
int AXFXReverbHiShutdown(struct AXFX_REVERBHI* reverb) {
    int enabled = OSDisableInterrupts();
    for (unsigned i = 0; i < 9; ++i) {
        if (reverb->rv.C[i].inputs) __AXFXFree(reverb->rv.C[i].inputs);
        if (reverb->rv.AP[i].inputs) __AXFXFree(reverb->rv.AP[i].inputs);
    }
    for (unsigned i = 0; i < 3; ++i)
        if (reverb->rv.preDelayLine[i]) __AXFXFree(reverb->rv.preDelayLine[i]);
    memset(&reverb->rv, 0, sizeof(reverb->rv));
    reverb->tempDisableFX = 1;
    OSRestoreInterrupts(enabled); return 1;
}
int AXFXReverbHiInit(struct AXFX_REVERBHI* reverb) {
    if (!isfinite(reverb->crosstalk) || reverb->crosstalk<0 || reverb->crosstalk>1 || !isfinite(reverb->time) || reverb->time < .01f || reverb->time > 10 ||
        !isfinite(reverb->coloration) || reverb->coloration < 0 || reverb->coloration > 1 ||
        !isfinite(reverb->mix) || reverb->mix < 0 || reverb->mix > 1 ||
        !isfinite(reverb->damping) || reverb->damping < 0 || reverb->damping > 1 ||
        !isfinite(reverb->preDelay) || reverb->preDelay < 0 || reverb->preDelay > .1f) return 0;
    int enabled = OSDisableInterrupts();
    struct AXFX_REVHI_WORK* rv = &reverb->rv;
    memset(rv, 0, sizeof(*rv));
    const int comb[3]={1789,1999,2333}, allpass[2]={433,149}, final[3]={47,73,67};
    for(unsigned c=0;c<3;++c) {
        for(unsigned i=0;i<3;++i) {
            unsigned index=c*3+i;
            if(!hi_line_create(&rv->C[index],comb[i]) || !hi_line_create(&rv->AP[index],i<2?allpass[i]:final[c])) goto failure;
            rv->combCoef[index]=powf(10,-3.0f*comb[i]/(32000*reverb->time));
        }
    }
    rv->crosstalk=reverb->crosstalk;
    rv->allPassCoeff = reverb->coloration; rv->level = reverb->mix;
    rv->damping = 1 - (.05f + .8f * fmaxf(.05f, reverb->damping));
    rv->preDelayTime = 32000 * reverb->preDelay;
    if (rv->preDelayTime) {
        for (unsigned c = 0; c < 3; ++c) {
            rv->preDelayLine[c] = __AXFXAlloc(rv->preDelayTime * sizeof(float));
            if (!rv->preDelayLine[c]) goto failure;
            memset(rv->preDelayLine[c], 0, rv->preDelayTime * sizeof(float));
            rv->preDelayPtr[c] = rv->preDelayLine[c];
        }
    }
    reverb->tempDisableFX = 0;
    OSRestoreInterrupts(enabled); return 1;
failure:
    AXFXReverbHiShutdown(reverb); OSRestoreInterrupts(enabled); return 0;
}
int AXFXReverbHiSettings(struct AXFX_REVERBHI* reverb) {
    int enabled = OSDisableInterrupts();
    AXFXReverbHiShutdown(reverb); int result = AXFXReverbHiInit(reverb);
    OSRestoreInterrupts(enabled); return result;
}
void AXFXReverbHiCallback(struct AXFX_BUFFERUPDATE* update, struct AXFX_REVERBHI* reverb) {
    if (reverb->tempDisableFX) return;
    struct AXFX_REVHI_WORK* rv = &reverb->rv;
    s32* channels[3] = {update->left, update->right, update->surround};
    float wet = .6f * rv->level, dry = .6f - wet;
    if(rv->crosstalk) {
        float cross=.5f*rv->crosstalk;
        for(unsigned n=0;n<160;++n) {
            float left=channels[0][n],right=channels[1][n];
            channels[0][n]=float_to_sample(fmaf(cross,right,(1-cross)*left));
            channels[1][n]=float_to_sample(fmaf(cross,left,(1-cross)*right));
        }
    }
    // Scalar translation of the SDK's paired comb, all-pass, damping, all-pass chain.
    for (unsigned c = 0; c < 3; ++c) {
        for (unsigned n = 0; n < 160; ++n) {
            float original = channels[c][n], input = original;
            if (rv->preDelayTime) {
                input = *rv->preDelayPtr[c]; *rv->preDelayPtr[c]++ = original;
                if (rv->preDelayPtr[c] == rv->preDelayLine[c] + rv->preDelayTime) rv->preDelayPtr[c] = rv->preDelayLine[c];
            }
            float summed = 0;
            for (unsigned i = 0; i < 3; ++i) {
                struct AXFX_REVHI_DELAYLINE* line = &rv->C[c*3+i];
                summed += hi_line_push(line, fmaf(rv->combCoef[c*3+i], line->lastOutput, input));
            }
            float signal=summed;
            for(unsigned i=0;i<3;++i) {
                if(i==2) {signal=fmaf(rv->damping,rv->lpLastout[c],.3f*signal);rv->lpLastout[c]=signal;}
                struct AXFX_REVHI_DELAYLINE* a=&rv->AP[c*3+i];
                float next=fmaf(rv->allPassCoeff,a->lastOutput,signal);
                signal=fmaf(-rv->allPassCoeff,next,a->lastOutput);hi_line_push(a,next);
            }
            channels[c][n] = float_to_sample(fmaf(wet, signal, dry * original));
        }
    }
}
