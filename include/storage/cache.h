/* =============================================================================
 * Nyota OS — Storage Block Cache Layer Interface (Phase 12)
 * Bounded memory LRU block cache with dirty tracking and writeback.
 * =========================================================================== */

#ifndef NYOTA_STORAGE_CACHE_H
#define NYOTA_STORAGE_CACHE_H

#include "types.h"
#include "storage/block.h"

#define BLOCK_CACHE_BLOCK_SIZE  1024
#define BLOCK_CACHE_CAPACITY    64     /* 64 blocks = 64 KiB memory budget */

#define CACHE_FLAG_VALID        (1 << 0)
#define CACHE_FLAG_DIRTY        (1 << 1)

typedef struct {
    block_device_t *bdev;
    uint64_t        lba;            /* Starting LBA of cached block */
    uint32_t        flags;
    uint64_t        last_accessed;  /* LRU timestamp */
    uint8_t         data[BLOCK_CACHE_BLOCK_SIZE];
} cache_block_t;

/* Block Cache APIs */
void block_cache_init(void);
int  block_cache_read(block_device_t *bdev, uint64_t lba, uint32_t count, void *buffer);
int  block_cache_write(block_device_t *bdev, uint64_t lba, uint32_t count, const void *buffer);
int  block_cache_flush(block_device_t *bdev);
int  block_cache_invalidate(block_device_t *bdev);

#endif /* NYOTA_STORAGE_CACHE_H */
