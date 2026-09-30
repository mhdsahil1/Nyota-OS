/* =============================================================================
 * Nyota OS — 2D Graphics Primitives & Font Engine
 * Core drawing functions, clipping, color abstraction, and bitmap text.
 * =========================================================================== */

#ifndef NYOTA_DRIVERS_GFX_H
#define NYOTA_DRIVERS_GFX_H

#include "types.h"
#include "drivers/framebuffer.h"

typedef struct {
    uint8_t b;
    uint8_t g;
    uint8_t r;
    uint8_t a;
} __attribute__((packed)) color_t;

#define COLOR_RGB(r, g, b)     ((color_t){ (uint8_t)(b), (uint8_t)(g), (uint8_t)(r), 0xFF })
#define COLOR_ARGB(a, r, g, b) ((color_t){ (uint8_t)(b), (uint8_t)(g), (uint8_t)(r), (uint8_t)(a) })
#define COLOR_HEX(hex)         ((color_t){ (uint8_t)((hex) & 0xFF), (uint8_t)(((hex) >> 8) & 0xFF), (uint8_t)(((hex) >> 16) & 0xFF), (uint8_t)(((hex) >> 24) ? (((hex) >> 24) & 0xFF) : 0xFF) })
#define COLOR_TO_UINT32(c)     (*(const uint32_t *)(&(c)))

/* 2D Graphics Primitives */
void gfx_init(void);
void gfx_put_pixel(uint32_t x, uint32_t y, color_t color);
void gfx_put_pixel_buf(uint32_t *buf, uint32_t pitch_pixels, uint32_t width, uint32_t height, uint32_t x, uint32_t y, color_t color);
void gfx_clear(color_t color);
void gfx_clear_buf(uint32_t *buf, uint32_t width, uint32_t height, color_t color);
void gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, color_t color);
void gfx_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, color_t color);
void gfx_fill_rect_buf(uint32_t *buf, uint32_t pitch_pixels, uint32_t max_w, uint32_t max_h, uint32_t x, uint32_t y, uint32_t w, uint32_t h, color_t color);
void gfx_line(int x0, int y0, int x1, int y1, color_t color);
void gfx_line_buf(uint32_t *buf, uint32_t pitch_pixels, uint32_t max_w, uint32_t max_h, int x0, int y0, int x1, int y1, color_t color);
void gfx_draw_char(uint32_t x, uint32_t y, char c, color_t fg, color_t bg);
void gfx_draw_char_buf(uint32_t *buf, uint32_t pitch_pixels, uint32_t max_w, uint32_t max_h, uint32_t x, uint32_t y, char c, color_t fg, color_t bg);
void gfx_draw_text(uint32_t x, uint32_t y, const char *str, color_t fg, color_t bg);
void gfx_draw_text_buf(uint32_t *buf, uint32_t pitch_pixels, uint32_t max_w, uint32_t max_h, uint32_t x, uint32_t y, const char *str, color_t fg, color_t bg);

/* Test screen generator (Section 8 requirement) */
void gfx_draw_test_screen(void);

#endif /* NYOTA_DRIVERS_GFX_H */
