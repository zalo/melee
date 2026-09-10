// Read the active linear DRM framebuffer, including imported g29 dma-bufs.
// A failed capture must not leave an old image looking like a fresh result.
#include <drm.h>
#include <drm_mode.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

int main(void)
{
    const char* output = "/tmp/melee-scanout.ppm";
    unlink(output);
    int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("open DRM");
        return 1;
    }
    drmModeRes* resources = drmModeGetResources(fd);
    if (!resources) {
        perror("get resources");
        return 1;
    }
    drmModeCrtc* crtc = NULL;
    for (int i = 0; i < resources->count_crtcs; ++i) {
        drmModeCrtc* candidate = drmModeGetCrtc(fd, resources->crtcs[i]);
        if (candidate && candidate->mode_valid && candidate->buffer_id) {
            crtc = candidate;
            break;
        }
        if (candidate) {
            drmModeFreeCrtc(candidate);
        }
    }
    drmModeFreeResources(resources);
    if (!crtc) {
        fprintf(stderr, "No active framebuffer\n");
        return 1;
    }
    drmModeFB* fb = drmModeGetFB(fd, crtc->buffer_id);
    if (!fb) {
        perror("get framebuffer");
        return 1;
    }
    if (fb->bpp != 32 || fb->width != 640 || fb->height != 480) {
        fprintf(stderr, "Unexpected framebuffer format\n");
        return 1;
    }
    const size_t size = (size_t) fb->pitch * fb->height;
    struct drm_mode_map_dumb map = { .handle = fb->handle };
    int dma = -1;
    void* mapped;
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) == 0) {
        mapped = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, map.offset);
    } else {
        if (drmPrimeHandleToFD(fd, fb->handle, DRM_CLOEXEC | DRM_RDWR, &dma)) {
            perror("export framebuffer");
            return 1;
        }
        struct dma_buf_sync sync = { .flags = DMA_BUF_SYNC_START |
                                              DMA_BUF_SYNC_READ };
        if (ioctl(dma, DMA_BUF_IOCTL_SYNC, &sync)) {
            perror("sync framebuffer");
            return 1;
        }
        mapped = mmap(NULL, size, PROT_READ, MAP_SHARED, dma, 0);
    }
    if (mapped == MAP_FAILED) {
        perror("map framebuffer");
        return 1;
    }
    unsigned char* pixels = malloc(size);
    if (!pixels) {
        return 1;
    }
    memcpy(pixels, mapped, size);
    munmap(mapped, size);
    if (dma >= 0) {
        struct dma_buf_sync sync = { .flags = DMA_BUF_SYNC_END |
                                              DMA_BUF_SYNC_READ };
        if (ioctl(dma, DMA_BUF_IOCTL_SYNC, &sync)) {
            perror("finish sync");
            return 1;
        }
        close(dma);
    }
    FILE* f = fopen("/tmp/melee-scanout.ppm.partial", "wb");
    if (!f) {
        perror("open output");
        return 1;
    }
    fprintf(f, "P6\n%u %u\n255\n", fb->width, fb->height);
    for (unsigned y = 0; y < fb->height; ++y) {
        for (unsigned x = 0; x < fb->width; ++x) {
            const unsigned char* q = pixels + y * fb->pitch + x * 4;
            const unsigned char rgb[] = { q[2], q[1], q[0] };
            fwrite(rgb, 1, 3, f);
        }
    }
    const int failed = ferror(f);
    if (fclose(f) || failed ||
        rename("/tmp/melee-scanout.ppm.partial", output))
    {
        return 1;
    }
    fprintf(stderr, "Captured CRTC %u framebuffer %u (%s)\n", crtc->crtc_id,
            crtc->buffer_id, dma >= 0 ? "dma-buf" : "dumb");
    free(pixels);
    drmModeFreeFB(fb);
    drmModeFreeCrtc(crtc);
    close(fd);
    return 0;
}
