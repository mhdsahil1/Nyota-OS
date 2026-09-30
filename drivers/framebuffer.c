/* =============================================================================
 * Nyota OS — Framebuffer Driver
 * VBE / Bochs Graphics Adapter (BGA) detection, mode setup, and virtual mapping.
 * =========================================================================== */

#include "drivers/framebuffer.h"
#include "drivers/gfx.h"
#include "drivers/pci.h"
#include "paging.h"
#include "memory.h"
#include "io.h"
#include "serial.h"
#include "kernel.h"

/* Bochs VBE (BGA) I/O Register Ports */
#define VBE_DISPI_IOPORT_INDEX      0x01CE
#define VBE_DISPI_IOPORT_DATA       0x01CF

#define VBE_DISPI_INDEX_ID          0x0
#define VBE_DISPI_INDEX_XRES        0x1
#define VBE_DISPI_INDEX_YRES        0x2
#define VBE_DISPI_INDEX_BPP         0x3
#define VBE_DISPI_INDEX_ENABLE      0x4
#define VBE_DISPI_INDEX_BANK        0x5
#define VBE_DISPI_INDEX_VIRT_WIDTH  0x6
#define VBE_DISPI_INDEX_VIRT_HEIGHT 0x7
#define VBE_DISPI_INDEX_X_OFFSET    0x8
#define VBE_DISPI_INDEX_Y_OFFSET    0x9

#define VBE_DISPI_DISABLED          0x00
#define VBE_DISPI_ENABLED           0x01
#define VBE_DISPI_LFB_ENABLED       0x40
#define VBE_DISPI_NOCLEARMEM        0x80

/* Bootloader Hand-off Block (Stage 2 at 0x6000) */
typedef struct {
    uint64_t phys_base;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t format;
    uint32_t is_valid;
} __attribute__((packed)) boot_fb_info_t;

static framebuffer_t g_fb = {0};

static inline void bga_write_reg(uint16_t index, uint16_t value) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    outw(VBE_DISPI_IOPORT_DATA, value);
}

static inline uint16_t bga_read_reg(uint16_t index) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    return inw(VBE_DISPI_IOPORT_DATA);
}

static bool bga_is_available(void) {
    uint16_t id = bga_read_reg(VBE_DISPI_INDEX_ID);
    return (id >= 0xB0C0 && id <= 0xB0C6);
}

static void bga_set_video_mode(uint32_t width, uint32_t height, uint32_t bpp) {
    bga_write_reg(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    bga_write_reg(VBE_DISPI_INDEX_XRES, (uint16_t)width);
    bga_write_reg(VBE_DISPI_INDEX_YRES, (uint16_t)height);
    bga_write_reg(VBE_DISPI_INDEX_BPP, (uint16_t)bpp);
    bga_write_reg(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);
}

bool framebuffer_init(void) {
    memset(&g_fb, 0, sizeof(framebuffer_t));

    uint64_t phys_base = 0;
    uint32_t width = 1024;
    uint32_t height = 768;
    uint32_t pitch = 1024 * 4;
    uint32_t bpp = 32;
    uint32_t format = FB_FORMAT_ARGB8888;

    /* 1. Check Bootloader Stage 2 VBE block at physical 0x6000 */
    boot_fb_info_t *boot_fb = (boot_fb_info_t *)0x6000;
    if (boot_fb->is_valid == 1 && boot_fb->phys_base != 0 && boot_fb->width > 0) {
        phys_base = boot_fb->phys_base;
        width     = boot_fb->width;
        height    = boot_fb->height;
        pitch     = boot_fb->pitch ? boot_fb->pitch : (width * (boot_fb->bpp / 8));
        bpp       = boot_fb->bpp ? boot_fb->bpp : 32;
        format    = boot_fb->format ? boot_fb->format : FB_FORMAT_ARGB8888;
        serial_write("[GFX] Discovered VBE framebuffer from bootloader at 0x6000\n");
    }

    /* 2. Check PCI for Display Controller (Vendor 0x1234 / Class 0x03) */
    pci_device_t *pci_vga = pci_find_device(0x1234, 0x1111);
    if (!pci_vga) {
        pci_vga = pci_find_class(0x03, 0x00);
    }

    if (pci_vga && pci_vga->bar[0] != 0) {
        if (phys_base == 0) {
            phys_base = pci_vga->bar[0];
        }
        pci_enable_bus_master(pci_vga);
    }

    /* 3. Configure Bochs Graphics Adapter if available */
    if (bga_is_available()) {
        width = 1024;
        height = 768;
        bpp = 32;
        pitch = width * 4;
        format = FB_FORMAT_ARGB8888;
        serial_write("[GFX] BGA controller detected. Setting 1024x768x32...\n");
        bga_set_video_mode(width, height, bpp);
    }

    /* Without bootloader mode data or a discovered display BAR, stay text-only. */
    if (phys_base == 0 || width == 0 || height == 0 || bpp != 32 ||
        format != FB_FORMAT_ARGB8888) {
        serial_write("[GFX] No supported 32-bit linear framebuffer; retaining text mode\n");
        return false;
    }
    if ((uint64_t)width * 4 != pitch || (uint64_t)height * pitch > 0xFFFFFFFFULL ||
        phys_base > 0x000FFFFFFFFFF000ULL - (uint64_t)height * pitch) {
        serial_write("[GFX] Invalid framebuffer geometry; retaining text mode\n");
        return false;
    }

    /* 4. Map physical framebuffer into kernel virtual address space */
    uint32_t total_size = height * pitch;
    size_t num_pages = (total_size + PAGE_SIZE - 1) / PAGE_SIZE;

    for (size_t i = 0; i < num_pages; i++) {
        uint64_t vaddr = KERNEL_FRAMEBUFFER_VIRT + (i * PAGE_SIZE);
        uint64_t paddr = phys_base + (i * PAGE_SIZE);
        if (!paging_map_page(vaddr, paddr, PAGE_PRESENT | PAGE_WRITABLE)) {
            serial_write("[GFX] ERROR: Failed to map framebuffer page\n");
            return false;
        }
    }

    g_fb.address     = KERNEL_FRAMEBUFFER_VIRT;
    g_fb.phys_addr   = phys_base;
    g_fb.width       = width;
    g_fb.height      = height;
    g_fb.pitch       = pitch;
    g_fb.bpp         = bpp;
    g_fb.format      = format;
    g_fb.size        = total_size;
    g_fb.initialized = true;

    serial_write("[GFX] Framebuffer active: ");
    serial_write_dec(g_fb.width); serial_write("x");
    serial_write_dec(g_fb.height); serial_write("x");
    serial_write_dec(g_fb.bpp); serial_write(" at phys ");
    serial_write_hex(g_fb.phys_addr); serial_write("\n");

    /* 5. Initialize 2D Graphics and draw Stage 2 test screen */
    gfx_init();
    gfx_draw_test_screen();

    return true;
}

framebuffer_t *framebuffer_get_info(void) {
    if (!g_fb.initialized) return NULL;
    return &g_fb;
}

bool framebuffer_is_active(void) {
    return g_fb.initialized;
}

void framebuffer_flip(const void *back_buffer, size_t size) {
    if (!g_fb.initialized || !back_buffer || size == 0) return;
    size_t row_bytes = (size_t)g_fb.width * sizeof(uint32_t);
    size_t required = row_bytes * g_fb.height;
    if (size < required) return;
    uint8_t *dst = (uint8_t *)g_fb.address;
    const uint8_t *src = (const uint8_t *)back_buffer;
    for (uint32_t y = 0; y < g_fb.height; y++) {
        memcpy(dst + (size_t)y * g_fb.pitch, src + (size_t)y * row_bytes, row_bytes);
    }
}
