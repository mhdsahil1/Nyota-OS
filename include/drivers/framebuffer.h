/* =============================================================================
 * Nyota OS — Framebuffer Abstraction
 * Physical and virtual framebuffer management, display modes, and video memory.
 * =========================================================================== */

#ifndef NYOTA_DRIVERS_FRAMEBUFFER_H
#define NYOTA_DRIVERS_FRAMEBUFFER_H

#include "types.h"

#define FB_FORMAT_RGB888    1
#define FB_FORMAT_BGR888    2
#define FB_FORMAT_ARGB8888  3
#define FB_FORMAT_RGBA8888  4

#define KERNEL_FRAMEBUFFER_VIRT 0xFFFFFFFFA0000000ULL

typedef struct {
    uint64_t address;     /* Virtual address in kernel space */
    uint64_t phys_addr;   /* Physical base address */
    uint32_t width;       /* Width in pixels (e.g. 1024) */
    uint32_t height;      /* Height in pixels (e.g. 768) */
    uint32_t pitch;       /* Scanline pitch in bytes (e.g. 4096) */
    uint32_t bpp;         /* Bits per pixel (e.g. 32) */
    uint32_t format;      /* Pixel format (e.g. FB_FORMAT_ARGB8888) */
    uint32_t size;        /* Total buffer size in bytes */
    bool     initialized; /* Whether graphics/framebuffer is active */
} framebuffer_t;

/* Public Framebuffer APIs */
bool framebuffer_init(void);
framebuffer_t *framebuffer_get_info(void);
bool framebuffer_is_active(void);
void framebuffer_flip(const void *back_buffer, size_t size);

#endif /* NYOTA_DRIVERS_FRAMEBUFFER_H */
