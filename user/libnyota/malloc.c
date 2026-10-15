/* =============================================================================
 * Nyota OS — Userspace Dynamic Memory Allocator (malloc.c)
 * Provides standard malloc, free, calloc, and realloc backed by sbrk().
 * Features chunk headers, first-fit allocation, splitting, bidirectional
 * coalescing, and 16-byte alignment.
 * =========================================================================== */

#include "libnyota.h"

#define HEAP_ALIGNMENT 16
#define ALIGN(size) (((size) + (HEAP_ALIGNMENT - 1)) & ~(HEAP_ALIGNMENT - 1))

typedef struct mem_chunk {
    size_t size;               /* Payload size in bytes (excluding chunk header) */
    bool is_free;              /* true if chunk is free, false if in-use */
    struct mem_chunk *next;    /* Next chunk in memory list */
    struct mem_chunk *prev;    /* Previous chunk in memory list */
} mem_chunk_t;

#define CHUNK_HEADER_SIZE ALIGN(sizeof(mem_chunk_t))

static mem_chunk_t *heap_head = NULL;
static mem_chunk_t *heap_tail = NULL;

#define SBRK_CHUNK_MIN (16 * 1024) /* 16 KiB minimum sbrk request to reduce syscalls */

static mem_chunk_t *request_space(size_t size) {
    size_t req_bytes = size + CHUNK_HEADER_SIZE;
    if (req_bytes < SBRK_CHUNK_MIN) {
        req_bytes = SBRK_CHUNK_MIN;
    }
    req_bytes = ALIGN(req_bytes);

    void *p = sbrk((intptr_t)req_bytes);
    if (p == (void *)-1 || p == NULL) {
        return NULL;
    }

    mem_chunk_t *chunk = (mem_chunk_t *)p;
    chunk->size = req_bytes - CHUNK_HEADER_SIZE;
    chunk->is_free = true;
    chunk->next = NULL;
    chunk->prev = heap_tail;

    if (heap_tail) {
        heap_tail->next = chunk;
    }
    heap_tail = chunk;
    if (!heap_head) {
        heap_head = chunk;
    }

    return chunk;
}

static void coalesce_chunk(mem_chunk_t *chunk) {
    if (!chunk || !chunk->is_free) return;

    /* Merge with next if next is contiguous and free */
    if (chunk->next && chunk->next->is_free) {
        uint8_t *expected_next = (uint8_t *)chunk + CHUNK_HEADER_SIZE + chunk->size;
        if ((uint8_t *)chunk->next == expected_next) {
            chunk->size += CHUNK_HEADER_SIZE + chunk->next->size;
            chunk->next = chunk->next->next;
            if (chunk->next) {
                chunk->next->prev = chunk;
            } else {
                heap_tail = chunk;
            }
        }
    }

    /* Merge with prev if prev is contiguous and free */
    if (chunk->prev && chunk->prev->is_free) {
        uint8_t *expected_curr = (uint8_t *)chunk->prev + CHUNK_HEADER_SIZE + chunk->prev->size;
        if ((uint8_t *)chunk == expected_curr) {
            chunk->prev->size += CHUNK_HEADER_SIZE + chunk->size;
            chunk->prev->next = chunk->next;
            if (chunk->next) {
                chunk->next->prev = chunk->prev;
            } else {
                heap_tail = chunk->prev;
            }
        }
    }
}

void *malloc(size_t size) {
    if (size == 0) return NULL;

    size = ALIGN(size);

    /* Search for first matching free chunk */
    mem_chunk_t *curr = heap_head;
    while (curr) {
        if (curr->is_free && curr->size >= size) {
            /* Check if we can split */
            if (curr->size >= size + CHUNK_HEADER_SIZE + HEAP_ALIGNMENT) {
                size_t remainder = curr->size - size - CHUNK_HEADER_SIZE;
                mem_chunk_t *split = (mem_chunk_t *)((uint8_t *)curr + CHUNK_HEADER_SIZE + size);
                split->size = remainder;
                split->is_free = true;
                split->next = curr->next;
                split->prev = curr;
                if (curr->next) {
                    curr->next->prev = split;
                } else {
                    heap_tail = split;
                }
                curr->next = split;
                curr->size = size;
            }
            curr->is_free = false;
            return (void *)((uint8_t *)curr + CHUNK_HEADER_SIZE);
        }
        curr = curr->next;
    }

    /* No chunk found: request from kernel */
    mem_chunk_t *chunk = request_space(size);
    if (!chunk) return NULL;

    /* Split if requested chunk is larger than requested size */
    if (chunk->size >= size + CHUNK_HEADER_SIZE + HEAP_ALIGNMENT) {
        size_t remainder = chunk->size - size - CHUNK_HEADER_SIZE;
        mem_chunk_t *split = (mem_chunk_t *)((uint8_t *)chunk + CHUNK_HEADER_SIZE + size);
        split->size = remainder;
        split->is_free = true;
        split->next = chunk->next;
        split->prev = chunk;
        if (chunk->next) {
            chunk->next->prev = split;
        } else {
            heap_tail = split;
        }
        chunk->next = split;
        chunk->size = size;
    }

    chunk->is_free = false;
    return (void *)((uint8_t *)chunk + CHUNK_HEADER_SIZE);
}

void free(void *ptr) {
    if (!ptr) return;

    mem_chunk_t *chunk = (mem_chunk_t *)((uint8_t *)ptr - CHUNK_HEADER_SIZE);
    chunk->is_free = true;

    coalesce_chunk(chunk);
}

void *calloc(size_t nmemb, size_t size) {
    if (nmemb == 0 || size == 0) return NULL;
    if (nmemb > (size_t)-1 / size) return NULL; /* overflow */

    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    size = ALIGN(size);
    mem_chunk_t *chunk = (mem_chunk_t *)((uint8_t *)ptr - CHUNK_HEADER_SIZE);

    if (chunk->size >= size) {
        return ptr;
    }

    /* Try in-place expansion if next chunk is free and contiguous */
    if (chunk->next && chunk->next->is_free) {
        uint8_t *expected_next = (uint8_t *)chunk + CHUNK_HEADER_SIZE + chunk->size;
        if ((uint8_t *)chunk->next == expected_next &&
            (chunk->size + CHUNK_HEADER_SIZE + chunk->next->size) >= size) {
            size_t combined = chunk->size + CHUNK_HEADER_SIZE + chunk->next->size;
            chunk->next = chunk->next->next;
            if (chunk->next) {
                chunk->next->prev = chunk;
            } else {
                heap_tail = chunk;
            }
            chunk->size = combined;

            /* Split remainder if possible */
            if (chunk->size >= size + CHUNK_HEADER_SIZE + HEAP_ALIGNMENT) {
                size_t remainder = chunk->size - size - CHUNK_HEADER_SIZE;
                mem_chunk_t *split = (mem_chunk_t *)((uint8_t *)chunk + CHUNK_HEADER_SIZE + size);
                split->size = remainder;
                split->is_free = true;
                split->next = chunk->next;
                split->prev = chunk;
                if (chunk->next) {
                    chunk->next->prev = split;
                } else {
                    heap_tail = split;
                }
                chunk->next = split;
                chunk->size = size;
            }
            return ptr;
        }
    }

    /* Allocate new buffer, copy, free old */
    void *new_ptr = malloc(size);
    if (!new_ptr) return NULL;

    size_t copy_len = (chunk->size < size) ? chunk->size : size;
    memcpy(new_ptr, ptr, copy_len);
    free(ptr);

    return new_ptr;
}
