/* =============================================================================
 * Nyota OS — Kernel Dynamic Memory Allocator (Heap)
 * kmalloc(), kfree(), kcalloc(), krealloc() backed by virtual memory and PMM.
 * =========================================================================== */

#include "heap.h"
#include "paging.h"
#include "pmm.h"
#include "memory.h"
#include "kernel.h"
#include "vga.h"

static uint64_t heap_start_addr = KERNEL_HEAP_START;
static uint64_t heap_end_addr = KERNEL_HEAP_START;
static heap_block_t *heap_head = NULL;

/* ── Heap Expansion ───────────────────────────────────────────────────────── */

static bool heap_expand(size_t minimum_bytes) {
    /* Expand in chunks of at least 64 KiB or page-aligned required bytes */
    size_t chunk_size = 64 * 1024ULL;
    if (minimum_bytes > chunk_size) {
        chunk_size = PAGE_ALIGN_UP(minimum_bytes);
    }

    size_t num_pages = chunk_size / PAGE_SIZE;
    uint64_t old_end = heap_end_addr;

    for (size_t i = 0; i < num_pages; i++) {
        void *phys_frame = pmm_alloc_page();
        if (!phys_frame) {
            kwarn("heap_expand: out of physical memory");
            return false;
        }

        uint64_t virt_addr = old_end + (i * PAGE_SIZE);
        if (!paging_map_page(virt_addr, (uint64_t)phys_frame, PAGE_PRESENT | PAGE_WRITABLE)) {
            kwarn("heap_expand: failed to map page");
            pmm_free_page(phys_frame);
            return false;
        }
    }

    heap_end_addr += chunk_size;

    /* Create new free block from expanded space */
    heap_block_t *new_block = (heap_block_t *)old_end;
    new_block->magic = HEAP_FREE_MAGIC;
    new_block->size = chunk_size - sizeof(heap_block_t);
    new_block->is_free = true;
    new_block->next = NULL;

    /* Append to tail of block list */
    if (!heap_head) {
        heap_head = new_block;
        new_block->prev = NULL;
    } else {
        heap_block_t *tail = heap_head;
        while (tail->next) {
            tail = tail->next;
        }
        tail->next = new_block;
        new_block->prev = tail;

        /* Merge with previous block if it is also free */
        if (tail->is_free) {
            tail->size += sizeof(heap_block_t) + new_block->size;
            tail->next = NULL;
        }
    }

    return true;
}

/* ── Initialization ───────────────────────────────────────────────────────── */

void heap_init(void) {
    heap_start_addr = KERNEL_HEAP_START;
    heap_end_addr = KERNEL_HEAP_START;
    heap_head = NULL;

    /* Map initial 1 MB heap */
    size_t num_pages = KERNEL_HEAP_INITIAL_SIZE / PAGE_SIZE;

    for (size_t i = 0; i < num_pages; i++) {
        void *phys_frame = pmm_alloc_page();
        if (!phys_frame) {
            kernel_panic("heap_init: failed to allocate physical frame for heap");
        }

        uint64_t virt_addr = heap_start_addr + (i * PAGE_SIZE);
        if (!paging_map_page(virt_addr, (uint64_t)phys_frame, PAGE_PRESENT | PAGE_WRITABLE)) {
            kernel_panic("heap_init: failed to map initial heap page");
        }
    }

    heap_end_addr = heap_start_addr + KERNEL_HEAP_INITIAL_SIZE;

    /* Single initial free block */
    heap_head = (heap_block_t *)heap_start_addr;
    heap_head->magic = HEAP_FREE_MAGIC;
    heap_head->size = KERNEL_HEAP_INITIAL_SIZE - sizeof(heap_block_t);
    heap_head->is_free = true;
    heap_head->next = NULL;
    heap_head->prev = NULL;
}

/* ── kmalloc & kfree ──────────────────────────────────────────────────────── */

void *kmalloc(size_t size) {
    if (size == 0) return NULL;

    /* Align requested size to 16 bytes */
    size_t aligned_size = (size + (HEAP_ALIGNMENT - 1)) & ~(HEAP_ALIGNMENT - 1);

    heap_block_t *curr = heap_head;

    while (curr) {
        if (curr->is_free && curr->size >= aligned_size) {
            /* Check if we can split this block */
            if (curr->size >= aligned_size + sizeof(heap_block_t) + HEAP_ALIGNMENT) {
                heap_block_t *split_block = (heap_block_t *)((uint8_t *)curr + sizeof(heap_block_t) + aligned_size);
                split_block->magic = HEAP_FREE_MAGIC;
                split_block->size = curr->size - aligned_size - sizeof(heap_block_t);
                split_block->is_free = true;
                split_block->next = curr->next;
                split_block->prev = curr;

                if (curr->next) {
                    curr->next->prev = split_block;
                }
                curr->next = split_block;
                curr->size = aligned_size;
            }

            curr->is_free = false;
            curr->magic = HEAP_ALLOC_MAGIC;
            return (void *)((uint8_t *)curr + sizeof(heap_block_t));
        }
        curr = curr->next;
    }

    /* Out of heap space: expand heap and retry */
    if (heap_expand(aligned_size + sizeof(heap_block_t))) {
        return kmalloc(size);
    }

    kwarn("kmalloc: allocation failed, heap full");
    return NULL;
}

void kfree(void *ptr) {
    if (!ptr) return;

    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));

    if (block->magic != HEAP_ALLOC_MAGIC) {
        kwarn("kfree: heap corruption or invalid pointer detected");
        return;
    }

    block->magic = HEAP_FREE_MAGIC;
    block->is_free = true;

    /* Coalesce with next block if free */
    if (block->next && block->next->is_free) {
        block->size += sizeof(heap_block_t) + block->next->size;
        block->next = block->next->next;
        if (block->next) {
            block->next->prev = block;
        }
    }

    /* Coalesce with previous block if free */
    if (block->prev && block->prev->is_free) {
        block->prev->size += sizeof(heap_block_t) + block->size;
        block->prev->next = block->next;
        if (block->next) {
            block->next->prev = block->prev;
        }
    }
}

void *kcalloc(size_t count, size_t size) {
    size_t total = count * size;
    if (count != 0 && total / count != size) {
        /* Overflow check */
        return NULL;
    }

    void *ptr = kmalloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *krealloc(void *ptr, size_t size) {
    if (!ptr) {
        return kmalloc(size);
    }
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }

    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));
    if (block->magic != HEAP_ALLOC_MAGIC) {
        kwarn("krealloc: invalid pointer or corrupted heap block");
        return NULL;
    }

    if (block->size >= size) {
        return ptr;
    }

    void *new_ptr = kmalloc(size);
    if (!new_ptr) {
        return NULL;
    }

    memcpy(new_ptr, ptr, block->size);
    kfree(ptr);
    return new_ptr;
}

/* ── Heap Diagnostics & Statistics ────────────────────────────────────────── */

uint64_t heap_get_start(void) {
    return heap_start_addr;
}

uint64_t heap_get_end(void) {
    return heap_end_addr;
}

uint64_t heap_get_total(void) {
    return heap_end_addr - heap_start_addr;
}

uint64_t heap_get_used(void) {
    uint64_t used = 0;
    heap_block_t *curr = heap_head;
    while (curr) {
        if (!curr->is_free) {
            used += curr->size + sizeof(heap_block_t);
        }
        curr = curr->next;
    }
    return used;
}

uint64_t heap_get_free(void) {
    uint64_t free_bytes = 0;
    heap_block_t *curr = heap_head;
    while (curr) {
        if (curr->is_free) {
            free_bytes += curr->size;
        }
        curr = curr->next;
    }
    return free_bytes;
}

void heap_print_stats(void) {
    vga_println("Kernel Heap");
    vga_println("-------------------------");
    vga_print("Start : ");
    vga_print_hex(heap_start_addr);
    vga_println("");

    vga_print("End   : ");
    vga_print_hex(heap_end_addr);
    vga_println("");

    vga_print("Total : ");
    vga_print_dec((heap_end_addr - heap_start_addr) / 1024);
    vga_println(" KB");

    vga_print("Used  : ");
    vga_print_dec(heap_get_used() / 1024);
    vga_println(" KB");

    vga_print("Free  : ");
    vga_print_dec(heap_get_free() / 1024);
    vga_println(" KB");
}
