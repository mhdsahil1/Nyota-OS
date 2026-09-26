/* =============================================================================
 * Nyota OS — Physical Memory Manager (PMM)
 * Page frame allocation based on BIOS E820 memory map and bitmap allocator.
 * =========================================================================== */

#include "pmm.h"
#include "kernel.h"
#include "memory.h"
#include "vga.h"

/* Linker symbols */
extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];

/* Internal state */
static uint8_t *pmm_bitmap = NULL;
static uint64_t pmm_total_frames = 0;
static uint64_t pmm_used_frames = 0;
static uint64_t pmm_free_frames = 0;
static uint64_t pmm_total_bytes = 0;
static uint64_t pmm_usable_bytes = 0;
static uint64_t pmm_highest_addr = 0;

static uint32_t mmap_entry_count = 0;
static e820_entry_t *mmap_entries = NULL;

/* ── Bitmap Helper Operations ─────────────────────────────────────────────── */

static inline void bitmap_set(uint64_t frame) {
    pmm_bitmap[frame / 8] |= (uint8_t)(1 << (frame % 8));
}

static inline void bitmap_clear(uint64_t frame) {
    pmm_bitmap[frame / 8] &= (uint8_t)(~(1 << (frame % 8)));
}

static inline bool bitmap_test(uint64_t frame) {
    return (pmm_bitmap[frame / 8] & (1 << (frame % 8))) != 0;
}

/* Mark a physical address range as USED (1) */
static void pmm_mark_range_used(uint64_t base, uint64_t length) {
    uint64_t start_frame = base / PAGE_SIZE;
    uint64_t end_frame = (base + length + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint64_t f = start_frame; f < end_frame && f < pmm_total_frames; f++) {
        if (!bitmap_test(f)) {
            bitmap_set(f);
            if (pmm_free_frames > 0) {
                pmm_free_frames--;
            }
            pmm_used_frames++;
        }
    }
}

/* Mark a physical address range as FREE (0) */
static void pmm_mark_range_free(uint64_t base, uint64_t length) {
    uint64_t start_frame = (base + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t end_frame = (base + length) / PAGE_SIZE;

    for (uint64_t f = start_frame; f < end_frame && f < pmm_total_frames; f++) {
        if (bitmap_test(f)) {
            bitmap_clear(f);
            if (pmm_used_frames > 0) {
                pmm_used_frames--;
            }
            pmm_free_frames++;
        }
    }
}

/* ── Initialization ───────────────────────────────────────────────────────── */

void pmm_init(void) {
    mmap_entry_count = *(volatile uint32_t *)E820_MAP_COUNT_ADDR;
    mmap_entries = (e820_entry_t *)E820_MAP_BUF_ADDR;

    pmm_total_bytes = 0;
    pmm_usable_bytes = 0;
    pmm_highest_addr = 0;

    /* 1. Parse BIOS E820 memory map */
    if (mmap_entry_count > 0 && mmap_entry_count <= 64) {
        /* Determine physical RAM boundary from usable regions below 4GB */
        for (uint32_t i = 0; i < mmap_entry_count; i++) {
            if (mmap_entries[i].type == MEMORY_USABLE && mmap_entries[i].base < 0x100000000ULL) {
                uint64_t region_end = mmap_entries[i].base + mmap_entries[i].length;
                if (region_end > pmm_highest_addr) {
                    pmm_highest_addr = region_end;
                }
                pmm_usable_bytes += mmap_entries[i].length;
            }
        }
        /* Round up to include any reserved firmware padding at top of RAM (e.g., 128 MB) */
        pmm_highest_addr = (pmm_highest_addr + 0x1FFFFFULL) & ~0x1FFFFFULL;
        pmm_total_bytes = pmm_highest_addr;
    } else {
        /* Fallback: assume 128 MB if E820 map was not retrieved */
        pmm_highest_addr = 128 * 1024 * 1024ULL;
        pmm_total_bytes = pmm_highest_addr;
        pmm_usable_bytes = pmm_highest_addr - (1024 * 1024ULL);
    }

    /* 2. Total frames to manage */
    pmm_total_frames = pmm_highest_addr / PAGE_SIZE;
    uint64_t bitmap_size = (pmm_total_frames + 7) / 8;

    /* 3. Place bitmap immediately after kernel in memory, aligned to page boundary */
    uint64_t kernel_end_addr = (uint64_t)_kernel_end;
    pmm_bitmap = (uint8_t *)PAGE_ALIGN_UP(kernel_end_addr);

    /* 4. Initially mark all frames as USED */
    memset(pmm_bitmap, 0xFF, bitmap_size);
    pmm_used_frames = pmm_total_frames;
    pmm_free_frames = 0;

    /* 5. Mark usable E820 regions as FREE */
    if (mmap_entry_count > 0 && mmap_entry_count <= 64) {
        for (uint32_t i = 0; i < mmap_entry_count; i++) {
            if (mmap_entries[i].type == MEMORY_USABLE) {
                pmm_mark_range_free(mmap_entries[i].base, mmap_entries[i].length);
            }
        }
    } else {
        /* Fallback: free 1 MB to 128 MB */
        pmm_mark_range_free(0x100000, pmm_highest_addr - 0x100000);
    }

    /* 6. Reserve critical low memory (0x00000000 - 0x00100000):
     * Includes IVT, BDA, bootloader, E820 buffer, initial page tables (0x1000-0x4000), stack (0x90000) */
    pmm_mark_range_used(0x00000000, 0x00100000);

    /* 7. Reserve kernel binary */
    uint64_t kernel_start_addr = (uint64_t)_kernel_start;
    pmm_mark_range_used(kernel_start_addr, kernel_end_addr - kernel_start_addr);

    /* 8. Reserve bitmap memory */
    uint64_t bitmap_pages = (bitmap_size + PAGE_SIZE - 1) / PAGE_SIZE;
    pmm_mark_range_used((uint64_t)pmm_bitmap, bitmap_pages * PAGE_SIZE);
}

/* ── Allocation & Freeing ─────────────────────────────────────────────────── */

void *pmm_alloc_page(void) {
    for (uint64_t i = 1; i < pmm_total_frames; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            pmm_used_frames++;
            pmm_free_frames--;

            void *page = (void *)(i * PAGE_SIZE);
            memset(page, 0, PAGE_SIZE);
            return page;
        }
    }
    return NULL;
}

void *pmm_alloc_pages(size_t count) {
    if (count == 0) return NULL;
    if (count == 1) return pmm_alloc_page();

    uint64_t consecutive = 0;
    uint64_t start_frame = 0;

    for (uint64_t i = 1; i < pmm_total_frames; i++) {
        if (!bitmap_test(i)) {
            if (consecutive == 0) {
                start_frame = i;
            }
            consecutive++;
            if (consecutive == count) {
                for (uint64_t j = start_frame; j < start_frame + count; j++) {
                    bitmap_set(j);
                }
                pmm_used_frames += count;
                pmm_free_frames -= count;

                void *page = (void *)(start_frame * PAGE_SIZE);
                memset(page, 0, count * PAGE_SIZE);
                return page;
            }
        } else {
            consecutive = 0;
        }
    }
    return NULL;
}

void pmm_free_page(void *address) {
    if (!address) return;

    uint64_t addr = (uint64_t)address;
    if ((addr & (PAGE_SIZE - 1)) != 0) {
        kwarn("pmm_free_page: unaligned address");
        return;
    }

    uint64_t frame = addr / PAGE_SIZE;
    if (frame >= pmm_total_frames) {
        kwarn("pmm_free_page: out of bounds");
        return;
    }

    if (!bitmap_test(frame)) {
        kwarn("pmm_free_page: double free detected");
        return;
    }

    bitmap_clear(frame);
    pmm_used_frames--;
    pmm_free_frames++;
}

void pmm_free_pages(void *address, size_t count) {
    if (!address || count == 0) return;

    uint64_t addr = (uint64_t)address;
    for (size_t i = 0; i < count; i++) {
        pmm_free_page((void *)(addr + (i * PAGE_SIZE)));
    }
}

/* ── Statistics & Diagnostic Information ──────────────────────────────────── */

uint64_t pmm_total_memory(void) {
    return pmm_total_bytes;
}

uint64_t pmm_usable_memory(void) {
    return pmm_usable_bytes;
}

uint64_t pmm_used_memory(void) {
    return pmm_used_frames * PAGE_SIZE;
}

uint64_t pmm_free_memory(void) {
    return pmm_free_frames * PAGE_SIZE;
}

uint64_t pmm_total_pages(void) {
    return pmm_total_frames;
}

uint64_t pmm_free_pages_count(void) {
    return pmm_free_frames;
}

static const char *get_region_type_name(uint32_t type) {
    switch (type) {
        case MEMORY_USABLE:           return "USABLE";
        case MEMORY_RESERVED:         return "RESERVED";
        case MEMORY_ACPI:             return "ACPI";
        case MEMORY_ACPI_RECLAIMABLE: return "ACPI_RECLAIM";
        case MEMORY_BAD:              return "BAD_MEM";
        default:                      return "UNKNOWN";
    }
}

void pmm_print_mmap(void) {
    vga_println("Memory Map");
    vga_println("----------------------------------------");
    vga_println("Base                 Length               Type");
    vga_println("----------------------------------------");

    if (mmap_entry_count == 0 || mmap_entry_count > 64) {
        vga_println("No BIOS E820 memory map available.");
        return;
    }

    for (uint32_t i = 0; i < mmap_entry_count; i++) {
        vga_print_hex(mmap_entries[i].base);
        vga_print("   ");
        vga_print_hex(mmap_entries[i].length);
        vga_print("   ");
        vga_println(get_region_type_name(mmap_entries[i].type));
    }
    vga_println("----------------------------------------");
}

void pmm_print_stats(void) {
    vga_println("Physical Memory");
    vga_println("-------------------------");
    vga_print("Total : ");
    vga_print_dec(pmm_total_bytes / (1024 * 1024));
    vga_println(" MB");

    vga_print("Usable: ");
    vga_print_dec(pmm_usable_bytes / (1024 * 1024));
    vga_println(" MB");

    vga_print("Used  : ");
    vga_print_dec((pmm_used_frames * PAGE_SIZE) / (1024 * 1024));
    vga_println(" MB");

    vga_print("Free  : ");
    vga_print_dec((pmm_free_frames * PAGE_SIZE) / (1024 * 1024));
    vga_println(" MB");
}
