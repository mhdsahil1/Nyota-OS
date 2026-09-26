/* =============================================================================
 * Nyota OS — Kernel Dynamic Memory Allocator (Heap)
 * kmalloc(), kfree(), kcalloc(), krealloc() backed by virtual memory and PMM.
 * =========================================================================== */

#ifndef NYOTA_HEAP_H
#define NYOTA_HEAP_H

#include "types.h"

/* Heap virtual address configuration */
#define KERNEL_HEAP_START       0xFFFFFFFF90000000ULL
#define KERNEL_HEAP_INITIAL_SIZE (1024 * 1024ULL) /* 1 MB initial heap */
#define HEAP_ALIGNMENT          16ULL

/* Magic markers for corruption detection */
#define HEAP_ALLOC_MAGIC        0x4E594F5441ULL  /* "NYOTA" */
#define HEAP_FREE_MAGIC         0xDEADBEEFULL

/* Heap Block Header */
typedef struct heap_block {
    uint64_t magic;
    size_t size;
    bool is_free;
    struct heap_block *next;
    struct heap_block *prev;
} __attribute__((aligned(16))) heap_block_t;

/* Heap Management APIs */
void heap_init(void);

void *kmalloc(size_t size);
void *kcalloc(size_t count, size_t size);
void *krealloc(void *ptr, size_t size);
void kfree(void *ptr);

uint64_t heap_get_start(void);
uint64_t heap_get_end(void);
uint64_t heap_get_total(void);
uint64_t heap_get_used(void);
uint64_t heap_get_free(void);

void heap_print_stats(void);

#endif /* NYOTA_HEAP_H */
