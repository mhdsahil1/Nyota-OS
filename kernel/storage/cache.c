/* =============================================================================
 * Nyota OS — Storage Block Cache Layer Implementation (Phase 12)
 * Bounded-memory LRU block cache with dirty tracking, writeback, and
 * multicore concurrency safety.
 * =========================================================================== */

#include "storage/cache.h"
#include "storage/block.h"
#include "memory.h"
#include "kernel.h"
#include "spinlock.h"

static cache_block_t cache_table[BLOCK_CACHE_CAPACITY];
static uint64_t cache_timer = 0;
static spinlock_t cache_lock = SPINLOCK_INIT;

void block_cache_init(void) {
    uint64_t rflags = spinlock_acquire_irqsave(&cache_lock);
    for (size_t i = 0; i < BLOCK_CACHE_CAPACITY; i++) {
        cache_table[i].bdev = NULL;
        cache_table[i].lba = 0;
        cache_table[i].flags = 0;
        cache_table[i].last_accessed = 0;
        memset(cache_table[i].data, 0, BLOCK_CACHE_BLOCK_SIZE);
    }
    cache_timer = 0;
    spinlock_release_irqrestore(&cache_lock, rflags);
}

static cache_block_t *find_block(block_device_t *bdev, uint64_t lba) {
    for (size_t i = 0; i < BLOCK_CACHE_CAPACITY; i++) {
        if ((cache_table[i].flags & CACHE_FLAG_VALID) &&
            cache_table[i].bdev == bdev &&
            cache_table[i].lba == lba) {
            return &cache_table[i];
        }
    }
    return NULL;
}

/* Evict LRU block, writing back dirty data if required.
 * MUST be called with cache_lock held. */
static cache_block_t *get_victim_block(void) {
    /* 1. Look for an unused slot */
    for (size_t i = 0; i < BLOCK_CACHE_CAPACITY; i++) {
        if (!(cache_table[i].flags & CACHE_FLAG_VALID)) {
            return &cache_table[i];
        }
    }

    /* 2. Select block with lowest last_accessed timestamp */
    size_t victim_idx = 0;
    uint64_t min_time = cache_table[0].last_accessed;

    for (size_t i = 1; i < BLOCK_CACHE_CAPACITY; i++) {
        if (cache_table[i].last_accessed < min_time) {
            min_time = cache_table[i].last_accessed;
            victim_idx = i;
        }
    }

    cache_block_t *victim = &cache_table[victim_idx];

    /* 3. If victim is dirty, flush it to device */
    if (victim->flags & CACHE_FLAG_DIRTY) {
        if (victim->bdev) {
            uint32_t sec_count = BLOCK_CACHE_BLOCK_SIZE / victim->bdev->sector_size;
            if (sec_count == 0) sec_count = 2;
            block_device_write(victim->bdev, victim->lba, sec_count, victim->data);
        }
        victim->flags &= ~CACHE_FLAG_DIRTY;
    }

    victim->flags &= ~CACHE_FLAG_VALID;
    return victim;
}

int block_cache_read(block_device_t *bdev, uint64_t lba, uint32_t count, void *buffer) {
    if (!bdev || !buffer || count == 0) return BLOCK_ERR_INVAL;

    uint32_t sectors_per_block = BLOCK_CACHE_BLOCK_SIZE / bdev->sector_size;
    if (sectors_per_block == 0) sectors_per_block = 2;

    /* If read is not an exact cache block, bypass directly to device */
    if (count != sectors_per_block) {
        return block_device_read(bdev, lba, count, buffer);
    }

    uint64_t rflags = spinlock_acquire_irqsave(&cache_lock);
    cache_timer++;

    cache_block_t *blk = find_block(bdev, lba);
    if (blk) {
        /* Cache Hit */
        blk->last_accessed = cache_timer;
        memcpy(buffer, blk->data, BLOCK_CACHE_BLOCK_SIZE);
        spinlock_release_irqrestore(&cache_lock, rflags);
        return BLOCK_OK;
    }

    /* Cache Miss: Read from device into newly allocated block */
    cache_block_t *slot = get_victim_block();
    slot->bdev = bdev;
    slot->lba = lba;
    slot->flags = 0;
    slot->last_accessed = cache_timer;

    /* Perform read while lock is released to avoid holding lock during hardware I/O */
    spinlock_release_irqrestore(&cache_lock, rflags);

    int err = block_device_read(bdev, lba, sectors_per_block, slot->data);

    rflags = spinlock_acquire_irqsave(&cache_lock);
    if (err == 0) {
        slot->flags |= CACHE_FLAG_VALID;
        memcpy(buffer, slot->data, BLOCK_CACHE_BLOCK_SIZE);
    } else {
        slot->flags = 0; /* Invalidate slot on I/O error */
    }
    spinlock_release_irqrestore(&cache_lock, rflags);

    return err;
}

int block_cache_write(block_device_t *bdev, uint64_t lba, uint32_t count, const void *buffer) {
    if (!bdev || !buffer || count == 0) return BLOCK_ERR_INVAL;
    if (bdev->read_only) return BLOCK_ERR_ROFS;

    uint32_t sectors_per_block = BLOCK_CACHE_BLOCK_SIZE / bdev->sector_size;
    if (sectors_per_block == 0) sectors_per_block = 2;

    /* Non-standard block sizes bypass cache directly */
    if (count != sectors_per_block) {
        return block_device_write(bdev, lba, count, buffer);
    }

    uint64_t rflags = spinlock_acquire_irqsave(&cache_lock);
    cache_timer++;

    cache_block_t *blk = find_block(bdev, lba);
    if (!blk) {
        blk = get_victim_block();
        blk->bdev = bdev;
        blk->lba = lba;
        blk->flags = CACHE_FLAG_VALID;
    }

    memcpy(blk->data, buffer, BLOCK_CACHE_BLOCK_SIZE);
    blk->flags |= (CACHE_FLAG_VALID | CACHE_FLAG_DIRTY);
    blk->last_accessed = cache_timer;

    spinlock_release_irqrestore(&cache_lock, rflags);
    return BLOCK_OK;
}

int block_cache_flush(block_device_t *bdev) {
    uint64_t rflags = spinlock_acquire_irqsave(&cache_lock);

    for (size_t i = 0; i < BLOCK_CACHE_CAPACITY; i++) {
        if ((cache_table[i].flags & (CACHE_FLAG_VALID | CACHE_FLAG_DIRTY)) ==
            (CACHE_FLAG_VALID | CACHE_FLAG_DIRTY)) {
            if (bdev == NULL || cache_table[i].bdev == bdev) {
                block_device_t *d = cache_table[i].bdev;
                uint64_t l = cache_table[i].lba;
                uint8_t write_buf[BLOCK_CACHE_BLOCK_SIZE];
                memcpy(write_buf, cache_table[i].data, BLOCK_CACHE_BLOCK_SIZE);
                cache_table[i].flags &= ~CACHE_FLAG_DIRTY;

                spinlock_release_irqrestore(&cache_lock, rflags);

                uint32_t sec_count = BLOCK_CACHE_BLOCK_SIZE / d->sector_size;
                if (sec_count == 0) sec_count = 2;
                block_device_write(d, l, sec_count, write_buf);

                rflags = spinlock_acquire_irqsave(&cache_lock);
            }
        }
    }

    spinlock_release_irqrestore(&cache_lock, rflags);

    if (bdev) {
        block_device_flush(bdev);
    }
    return BLOCK_OK;
}

int block_cache_invalidate(block_device_t *bdev) {
    /* Flush dirty data before invalidating */
    block_cache_flush(bdev);

    uint64_t rflags = spinlock_acquire_irqsave(&cache_lock);
    for (size_t i = 0; i < BLOCK_CACHE_CAPACITY; i++) {
        if (bdev == NULL || cache_table[i].bdev == bdev) {
            cache_table[i].flags = 0;
            cache_table[i].bdev = NULL;
        }
    }
    spinlock_release_irqrestore(&cache_lock, rflags);
    return BLOCK_OK;
}
