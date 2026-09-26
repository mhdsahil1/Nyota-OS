/* =============================================================================
 * Nyota OS — Memory Subsystem Verification & Stress Tests
 * Tests PMM, Paging, and Heap allocation, alignment, expansion, and freeing.
 * =========================================================================== */

#include "memtest.h"
#include "pmm.h"
#include "paging.h"
#include "heap.h"
#include "vga.h"
#include "serial.h"
#include "memory.h"
#include "kernel.h"

void memtest_run_all(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_println("----------------------------------------");
    vga_println("       NYOTA MEMORY TEST SUITE          ");
    vga_println("----------------------------------------");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    /* ── Test 1: Physical Memory Manager (PMM) ────────────────────────────── */
    vga_print("[TEST 1/4] PMM frame allocator... ");
    serial_write("[MEMTEST] Testing PMM frame allocator...\n");

    void *f1 = pmm_alloc_page();
    if (!f1 || ((uint64_t)f1 & (PAGE_SIZE - 1)) != 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FAIL: Invalid frame pointer");
        return;
    }
    memset(f1, 0xAA, PAGE_SIZE);
    for (size_t i = 0; i < PAGE_SIZE; i++) {
        if (((uint8_t *)f1)[i] != 0xAA) {
            vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
            vga_println("FAIL: Frame memory verification error");
            return;
        }
    }

    void *f_multi = pmm_alloc_pages(4);
    if (!f_multi || ((uint64_t)f_multi & (PAGE_SIZE - 1)) != 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FAIL: Multi-page frame allocation error");
        return;
    }
    memset(f_multi, 0x55, 4 * PAGE_SIZE);

    pmm_free_page(f1);
    pmm_free_pages(f_multi, 4);

    /* Reallocate to verify reuse of freed frame */
    void *f_reuse = pmm_alloc_page();
    if (!f_reuse) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FAIL: Reallocating freed frame failed");
        return;
    }
    pmm_free_page(f_reuse);

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_println("PASS");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    /* ── Test 2: Paging & Virtual Translation ─────────────────────────────── */
    vga_print("[TEST 2/4] 4-level paging & VMM... ");
    serial_write("[MEMTEST] Testing 4-level paging & VMM...\n");

    uint64_t test_virt = 0xFFFFFFFF80800000ULL;
    void *phys_frame = pmm_alloc_page();
    if (!phys_frame) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FAIL: Out of physical memory for paging test");
        return;
    }

    if (!paging_map_page(test_virt, (uint64_t)phys_frame, PAGE_PRESENT | PAGE_WRITABLE)) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FAIL: paging_map_page failed");
        pmm_free_page(phys_frame);
        return;
    }

    /* Write 64-bit value to newly mapped virtual page */
    volatile uint64_t *ptr_virt = (volatile uint64_t *)test_virt;
    *ptr_virt = 0x123456789ABCDEF0ULL;
    if (*ptr_virt != 0x123456789ABCDEF0ULL) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FAIL: Virtual memory read/write mismatch");
        return;
    }

    /* Verify translation */
    uint64_t resolved_phys = paging_get_physical(test_virt);
    if (resolved_phys != (uint64_t)phys_frame) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FAIL: Virtual-to-physical translation mismatch");
        return;
    }

    /* Unmap page */
    if (!paging_unmap_page(test_virt)) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FAIL: paging_unmap_page failed");
        return;
    }
    pmm_free_page(phys_frame);

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_println("PASS");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    /* ── Test 3: Kernel Heap (kmalloc, kfree, kcalloc, krealloc) ──────────── */
    vga_print("[TEST 3/4] Kernel heap (kmalloc/kfree)... ");
    serial_write("[MEMTEST] Testing Kernel heap...\n");

    void *p1 = kmalloc(16);
    void *p2 = kmalloc(64);
    void *p3 = kmalloc(512);

    if (!p1 || !p2 || !p3) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FAIL: kmalloc returned NULL");
        return;
    }

    /* Alignment test */
    if (((uintptr_t)p1 & (HEAP_ALIGNMENT - 1)) != 0 ||
        ((uintptr_t)p2 & (HEAP_ALIGNMENT - 1)) != 0 ||
        ((uintptr_t)p3 & (HEAP_ALIGNMENT - 1)) != 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FAIL: kmalloc pointer unaligned");
        return;
    }

    /* Overlap test */
    if ((uint8_t *)p1 + 16 > (uint8_t *)p2 && (uint8_t *)p2 + 64 > (uint8_t *)p1) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FAIL: Overlapping allocations detected");
        return;
    }

    memset(p1, 0x11, 16);
    memset(p2, 0x22, 64);
    memset(p3, 0x33, 512);

    /* Free middle block and allocate to test reuse */
    kfree(p2);
    void *p4 = kmalloc(32);
    if (!p4) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FAIL: Reallocating in freed slot failed");
        return;
    }
    memset(p4, 0x44, 32);

    /* kcalloc test */
    uint32_t *c_arr = (uint32_t *)kcalloc(8, sizeof(uint32_t));
    if (!c_arr) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FAIL: kcalloc returned NULL");
        return;
    }
    for (int i = 0; i < 8; i++) {
        if (c_arr[i] != 0) {
            vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
            vga_println("FAIL: kcalloc memory not zeroed");
            return;
        }
    }

    /* krealloc test */
    p4 = krealloc(p4, 128);
    if (!p4 || ((uint8_t *)p4)[0] != 0x44) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_println("FAIL: krealloc data corrupted");
        return;
    }

    kfree(p1);
    kfree(p3);
    kfree(p4);
    kfree(c_arr);

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_println("PASS");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    /* ── Test 4: Heap Stress Test ─────────────────────────────────────────── */
    vga_print("[TEST 4/4] Heap stress test (100 blocks)... ");
    serial_write("[MEMTEST] Testing heap stress test...\n");

    #define STRESS_BLOCK_COUNT 100
    void *ptrs[STRESS_BLOCK_COUNT];

    for (int i = 0; i < STRESS_BLOCK_COUNT; i++) {
        size_t sz = 16 + (i * 8);
        ptrs[i] = kmalloc(sz);
        if (!ptrs[i]) {
            vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
            vga_println("FAIL: Stress allocation exhausted");
            return;
        }
        memset(ptrs[i], (uint8_t)(i & 0xFF), sz);
    }

    /* Verify all blocks */
    for (int i = 0; i < STRESS_BLOCK_COUNT; i++) {
        size_t sz = 16 + (i * 8);
        uint8_t expected = (uint8_t)(i & 0xFF);
        uint8_t *b = (uint8_t *)ptrs[i];
        for (size_t j = 0; j < sz; j++) {
            if (b[j] != expected) {
                vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
                vga_println("FAIL: Memory corruption during stress test");
                return;
            }
        }
    }

    /* Free all blocks */
    for (int i = 0; i < STRESS_BLOCK_COUNT; i++) {
        kfree(ptrs[i]);
    }

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_println("PASS");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    vga_println("----------------------------------------");
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_println("All Phase 3 memory tests PASSED!");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    serial_write("[MEMTEST] All memory tests PASSED successfully.\n");
}

void memtest_trigger_page_fault(void) {
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_println("Triggering controlled page fault access to 0x12345000...");
    serial_write("Triggering controlled page fault access to 0x12345000...\n");

    volatile uint64_t *unmapped = (volatile uint64_t *)0x12345000ULL;
    uint64_t val = *unmapped;
    (void)val;
}
