/* =============================================================================
 * Nyota OS — 2D Graphics Engine & Software Rasterizer
 * Pixel operations, rectangles, lines, bitmap font text, and test screen.
 * =========================================================================== */

#include "drivers/gfx.h"
#include "drivers/framebuffer.h"
#include "drivers/font8x16.h"
#include "memory.h"

static framebuffer_t *fb = NULL;

void gfx_init(void) {
    fb = framebuffer_get_info();
}

void gfx_put_pixel_buf(uint32_t *buf, uint32_t pitch_pixels, uint32_t width, uint32_t height, uint32_t x, uint32_t y, color_t color) {
    if (!buf || x >= width || y >= height) return;
    buf[y * pitch_pixels + x] = COLOR_TO_UINT32(color);
}

void gfx_put_pixel(uint32_t x, uint32_t y, color_t color) {
    if (!fb || !fb->initialized) return;
    gfx_put_pixel_buf((uint32_t *)fb->address, fb->pitch / 4, fb->width, fb->height, x, y, color);
}

void gfx_clear_buf(uint32_t *buf, uint32_t width, uint32_t height, color_t color) {
    if (!buf) return;
    uint32_t val = COLOR_TO_UINT32(color);
    size_t count = (size_t)width * height;
    for (size_t i = 0; i < count; i++) {
        buf[i] = val;
    }
}

void gfx_clear(color_t color) {
    if (!fb || !fb->initialized) return;
    gfx_fill_rect_buf((uint32_t *)fb->address, fb->pitch / sizeof(uint32_t), fb->width, fb->height,
                      0, 0, fb->width, fb->height, color);
}

void gfx_fill_rect_buf(uint32_t *buf, uint32_t pitch_pixels, uint32_t max_w, uint32_t max_h, uint32_t x, uint32_t y, uint32_t w, uint32_t h, color_t color) {
    if (!buf || x >= max_w || y >= max_h) return;
    if (w > max_w - x) w = max_w - x;
    if (h > max_h - y) h = max_h - y;

    uint32_t val = COLOR_TO_UINT32(color);
    for (uint32_t r = 0; r < h; r++) {
        uint32_t *row = &buf[(y + r) * pitch_pixels + x];
        for (uint32_t c = 0; c < w; c++) {
            row[c] = val;
        }
    }
}

void gfx_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, color_t color) {
    if (!fb || !fb->initialized) return;
    gfx_fill_rect_buf((uint32_t *)fb->address, fb->pitch / 4, fb->width, fb->height, x, y, w, h, color);
}

void gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, color_t color) {
    if (!fb || !fb->initialized || w == 0 || h == 0) return;
    if (x >= fb->width || y >= fb->height) return;
    uint32_t right = x + ((w - 1 > fb->width - 1 - x) ? fb->width - 1 - x : w - 1);
    uint32_t bottom = y + ((h - 1 > fb->height - 1 - y) ? fb->height - 1 - y : h - 1);
    gfx_fill_rect(x, y, right - x + 1, 1, color);
    gfx_fill_rect(x, bottom, right - x + 1, 1, color);
    gfx_fill_rect(x, y, 1, bottom - y + 1, color);
    gfx_fill_rect(right, y, 1, bottom - y + 1, color);
}

void gfx_line_buf(uint32_t *buf, uint32_t pitch_pixels, uint32_t max_w, uint32_t max_h, int x0, int y0, int x1, int y1, color_t color) {
    if (!buf) return;
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = ((dx > dy) ? dx : -dy) / 2;

    while (1) {
        if (x0 >= 0 && (uint32_t)x0 < max_w && y0 >= 0 && (uint32_t)y0 < max_h) {
            buf[y0 * pitch_pixels + x0] = COLOR_TO_UINT32(color);
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = err;
        if (e2 > -dx) { err -= dy; x0 += sx; }
        if (e2 <  dy) { err += dx; y0 += sy; }
    }
}

void gfx_line(int x0, int y0, int x1, int y1, color_t color) {
    if (!fb || !fb->initialized) return;
    gfx_line_buf((uint32_t *)fb->address, fb->pitch / 4, fb->width, fb->height, x0, y0, x1, y1, color);
}

void gfx_draw_char_buf(uint32_t *buf, uint32_t pitch_pixels, uint32_t max_w, uint32_t max_h, uint32_t x, uint32_t y, char c, color_t fg, color_t bg) {
    if (!buf || x >= max_w || y >= max_h) return;
    uint8_t ch = (uint8_t)c;
    const uint8_t *glyph = font8x16[ch];

    uint32_t fg_val = COLOR_TO_UINT32(fg);
    uint32_t bg_val = COLOR_TO_UINT32(bg);
    bool has_bg = (bg.a > 0);

    for (uint32_t row = 0; row < 16; row++) {
        if (y + row >= max_h) break;
        uint8_t bits = glyph[row];
        uint32_t *dst = &buf[(y + row) * pitch_pixels + x];
        for (uint32_t col = 0; col < 8; col++) {
            if (x + col >= max_w) break;
            if (bits & (1 << (7 - col))) {
                dst[col] = fg_val;
            } else if (has_bg) {
                dst[col] = bg_val;
            }
        }
    }
}

void gfx_draw_char(uint32_t x, uint32_t y, char c, color_t fg, color_t bg) {
    if (!fb || !fb->initialized) return;
    gfx_draw_char_buf((uint32_t *)fb->address, fb->pitch / 4, fb->width, fb->height, x, y, c, fg, bg);
}

void gfx_draw_text_buf(uint32_t *buf, uint32_t pitch_pixels, uint32_t max_w, uint32_t max_h, uint32_t x, uint32_t y, const char *str, color_t fg, color_t bg) {
    if (!buf || !str) return;
    uint32_t cur_x = x;
    uint32_t cur_y = y;

    while (*str) {
        if (*str == '\n') {
            cur_x = x;
            cur_y += 16;
            str++;
            continue;
        }
        if (*str == '\r') {
            cur_x = x;
            str++;
            continue;
        }
        if (*str == '\t') {
            cur_x = ((cur_x - x) / 32 + 1) * 32 + x;
            str++;
            continue;
        }
        gfx_draw_char_buf(buf, pitch_pixels, max_w, max_h, cur_x, cur_y, *str, fg, bg);
        cur_x += 8;
        str++;
    }
}

void gfx_draw_text(uint32_t x, uint32_t y, const char *str, color_t fg, color_t bg) {
    if (!fb || !fb->initialized) return;
    gfx_draw_text_buf((uint32_t *)fb->address, fb->pitch / 4, fb->width, fb->height, x, y, str, fg, bg);
}

/* ── Section 8: Graphics Initialization Test Screen ───────────────────────── */

void gfx_draw_test_screen(void) {
    if (!fb || !fb->initialized) return;

    /* 1. Deep modern slate background */
    color_t bg_color = COLOR_HEX(0x1E1E2E);
    gfx_clear(bg_color);

    /* 2. Top Title Banner */
    color_t banner_bg = COLOR_HEX(0x181825);
    color_t banner_border = COLOR_HEX(0x313244);
    gfx_fill_rect(20, 20, fb->width - 40, 90, banner_bg);
    gfx_rect(20, 20, fb->width - 40, 90, banner_border);

    color_t title_fg = COLOR_HEX(0x89B4FA);  /* Vibrant cyan-blue */
    color_t sub_fg   = COLOR_HEX(0xA6E3A1);  /* Soft mint green */
    color_t text_fg  = COLOR_HEX(0xCDD6F4);  /* Clean white */
    color_t transparent = COLOR_ARGB(0, 0, 0, 0);

    gfx_draw_text(40, 36, "NYOTA OS", title_fg, transparent);
    gfx_draw_text(40, 56, "Graphics Initialized", sub_fg, transparent);
    gfx_draw_text(40, 76, "Phase 10: Graphical User Interface & Desktop Environment", text_fg, transparent);

    /* 3. Specs Panel */
    gfx_fill_rect(20, 130, 320, 180, banner_bg);
    gfx_rect(20, 130, 320, 180, banner_border);

    gfx_draw_text(36, 146, "DISPLAY SPECIFICATIONS", title_fg, transparent);
    gfx_line(36, 166, 310, 166, banner_border);

    char res_buf[64];
    /* Format resolution string */
    char *p = res_buf;
    const char *lbl_res = "Resolution: 1024x768";
    if (fb->width == 800) lbl_res = "Resolution: 800x600";
    else if (fb->width == 640) lbl_res = "Resolution: 640x480";
    while (*lbl_res) *p++ = *lbl_res++;
    *p = '\0';
    gfx_draw_text(36, 180, res_buf, text_fg, transparent);

    char bpp_buf[32];
    p = bpp_buf;
    const char *lbl_bpp = "BPP       : 32 (ARGB8888)";
    while (*lbl_bpp) *p++ = *lbl_bpp++;
    *p = '\0';
    gfx_draw_text(36, 204, bpp_buf, text_fg, transparent);

    gfx_draw_text(36, 228, "Memory    : Linear Framebuffer", text_fg, transparent);
    gfx_draw_text(36, 252, "Buffering : Double Buffered", text_fg, transparent);
    gfx_draw_text(36, 276, "Status    : Ring 0 Kernel Ready", sub_fg, transparent);

    /* 4. Rectangles Test */
    gfx_fill_rect(360, 130, 300, 180, banner_bg);
    gfx_rect(360, 130, 300, 180, banner_border);
    gfx_draw_text(376, 146, "GEOMETRIC RECTANGLES", title_fg, transparent);
    gfx_line(376, 166, 630, 166, banner_border);

    color_t palette[6] = {
        COLOR_HEX(0xF38BA8), /* Red */
        COLOR_HEX(0xFAB387), /* Orange */
        COLOR_HEX(0xF9E2AF), /* Yellow */
        COLOR_HEX(0xA6E3A1), /* Green */
        COLOR_HEX(0x89B4FA), /* Blue */
        COLOR_HEX(0xCBA6F7)  /* Mauve */
    };

    for (int i = 0; i < 6; i++) {
        gfx_fill_rect(376 + i * 42, 180, 34, 40, palette[i]);
        gfx_rect(376 + i * 42, 230, 34, 40, palette[5 - i]);
    }
    gfx_draw_text(376, 280, "[Solid Rects]     [Outlines]", text_fg, transparent);

    /* 5. Lines Test */
    gfx_fill_rect(680, 130, fb->width - 700, 180, banner_bg);
    gfx_rect(680, 130, fb->width - 700, 180, banner_border);
    gfx_draw_text(696, 146, "BRESENHAM VECTOR LINES", title_fg, transparent);
    gfx_line(696, 166, fb->width - 50, 166, banner_border);

    int center_x = 680 + (fb->width - 700) / 2;
    int center_y = 230;
    int radius = 45;
    for (int deg = 0; deg < 360; deg += 30) {
        int lx = center_x;
        int ly = center_y;
        /* Simple starburst lines */
        if (deg == 0)   { lx += radius; }
        if (deg == 30)  { lx += radius * 7 / 8; ly += radius / 2; }
        if (deg == 60)  { lx += radius / 2;     ly += radius * 7 / 8; }
        if (deg == 90)  { ly += radius; }
        if (deg == 120) { lx -= radius / 2;     ly += radius * 7 / 8; }
        if (deg == 150) { lx -= radius * 7 / 8; ly += radius / 2; }
        if (deg == 180) { lx -= radius; }
        if (deg == 210) { lx -= radius * 7 / 8; ly -= radius / 2; }
        if (deg == 240) { lx -= radius / 2;     ly -= radius * 7 / 8; }
        if (deg == 270) { ly -= radius; }
        if (deg == 300) { lx += radius / 2;     ly -= radius * 7 / 8; }
        if (deg == 330) { lx += radius * 7 / 8; ly -= radius / 2; }
        gfx_line(center_x, center_y, lx, ly, palette[(deg / 30) % 6]);
    }

    /* 6. Typography & Font Verification */
    gfx_fill_rect(20, 330, fb->width - 40, 150, banner_bg);
    gfx_rect(20, 330, fb->width - 40, 150, banner_border);
    gfx_draw_text(36, 346, "TYPOGRAPHY & FONT SUB-ENGINE (8x16 Monospace)", title_fg, transparent);
    gfx_line(36, 366, fb->width - 50, 366, banner_border);

    gfx_draw_text(36, 380, "ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz 0123456789", text_fg, transparent);
    gfx_draw_text(36, 404, "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~ [NyotaOS Kernel Native Render]", sub_fg, transparent);
    gfx_draw_text(36, 428, "System ready for Userland Window Server, Compositor & Desktop Shell.", title_fg, transparent);
    gfx_draw_text(36, 452, "Milestone Target: Nyota OS v1.0.0", COLOR_HEX(0xF9E2AF), transparent);
}
