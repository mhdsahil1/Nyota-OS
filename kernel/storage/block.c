/* =============================================================================
 * Nyota OS — Unified Block Device Subsystem Implementation (Phase 12)
 * Manages registered block devices, partition routing, boundary validation,
 * and concurrency-safe sector read/write operations.
 * =========================================================================== */

#include "storage/block.h"
#include "memory.h"
#include "vga.h"
#include "kernel.h"
#include "spinlock.h"

static block_device_t *devices[MAX_BLOCK_DEVICES] = {0};
static size_t device_count = 0;
static spinlock_t bdev_table_lock = SPINLOCK_INIT;

/* Optional partition scan hook */
extern int partition_scan(block_device_t *disk);

void block_device_init(void) {
    uint64_t rflags = spinlock_acquire_irqsave(&bdev_table_lock);
    for (size_t i = 0; i < MAX_BLOCK_DEVICES; i++) {
        devices[i] = NULL;
    }
    device_count = 0;
    spinlock_release_irqrestore(&bdev_table_lock, rflags);
}

int block_device_register(block_device_t *dev) {
    if (!dev || !dev->read) {
        return BLOCK_ERR_INVAL;
    }

    if (dev->sector_size == 0) {
        dev->sector_size = 512;
    }
    if (dev->capacity_sectors == 0 && dev->sector_count > 0) {
        dev->capacity_sectors = dev->sector_count;
    }
    dev->sector_count = dev->capacity_sectors;
    dev->present = true;

    uint64_t rflags = spinlock_acquire_irqsave(&bdev_table_lock);

    if (device_count >= MAX_BLOCK_DEVICES) {
        spinlock_release_irqrestore(&bdev_table_lock, rflags);
        kwarn("block_device_register: device table full");
        return BLOCK_ERR_NODEV;
    }

    /* Check if already registered */
    for (size_t i = 0; i < device_count; i++) {
        if (devices[i] == dev) {
            spinlock_release_irqrestore(&bdev_table_lock, rflags);
            return BLOCK_OK;
        }
    }

    dev->id = (uint32_t)device_count;
    devices[device_count++] = dev;

    spinlock_release_irqrestore(&bdev_table_lock, rflags);

    /* If this is a physical disk (not a partition sub-device), scan for partitions */
    if (!dev->parent) {
        partition_scan(dev);
    }

    return BLOCK_OK;
}

int block_device_unregister(block_device_t *dev) {
    if (!dev) return BLOCK_ERR_INVAL;

    uint64_t rflags = spinlock_acquire_irqsave(&bdev_table_lock);
    int found_idx = -1;

    for (size_t i = 0; i < device_count; i++) {
        if (devices[i] == dev) {
            found_idx = (int)i;
            break;
        }
    }

    if (found_idx < 0) {
        spinlock_release_irqrestore(&bdev_table_lock, rflags);
        return BLOCK_ERR_NODEV;
    }

    dev->present = false;

    /* Shift remaining entries down */
    for (size_t i = (size_t)found_idx; i < device_count - 1; i++) {
        devices[i] = devices[i + 1];
        if (devices[i]) {
            devices[i]->id = (uint32_t)i;
        }
    }
    devices[device_count - 1] = NULL;
    device_count--;

    spinlock_release_irqrestore(&bdev_table_lock, rflags);
    return BLOCK_OK;
}

block_device_t *block_device_get(uint32_t id) {
    uint64_t rflags = spinlock_acquire_irqsave(&bdev_table_lock);
    block_device_t *ret = NULL;
    if (id < device_count) {
        ret = devices[id];
    }
    spinlock_release_irqrestore(&bdev_table_lock, rflags);
    return ret;
}

block_device_t *block_device_find_by_name(const char *name) {
    if (!name) return NULL;

    /* Skip leading "/dev/" if caller supplied full path */
    const char *lookup_name = name;
    if (strncmp(lookup_name, "/dev/", 5) == 0) {
        lookup_name += 5;
    }

    uint64_t rflags = spinlock_acquire_irqsave(&bdev_table_lock);
    block_device_t *found = NULL;

    for (size_t i = 0; i < device_count; i++) {
        if (devices[i] && strcmp(devices[i]->name, lookup_name) == 0) {
            found = devices[i];
            break;
        }
    }

    spinlock_release_irqrestore(&bdev_table_lock, rflags);
    return found;
}

size_t block_device_count(void) {
    uint64_t rflags = spinlock_acquire_irqsave(&bdev_table_lock);
    size_t cnt = device_count;
    spinlock_release_irqrestore(&bdev_table_lock, rflags);
    return cnt;
}

int block_device_read(block_device_t *dev, uint64_t lba, uint32_t count, void *buffer) {
    if (!dev || !buffer || count == 0) {
        return BLOCK_ERR_INVAL;
    }

    if (!dev->present) {
        return BLOCK_ERR_NODEV;
    }

    /* Integer overflow check */
    if (lba + count < lba) {
        kwarn("block_device_read: LBA overflow");
        return BLOCK_ERR_BOUNDS;
    }

    /* Capacity boundary check */
    if (dev->capacity_sectors > 0 && lba + count > dev->capacity_sectors) {
        kwarn("block_device_read: LBA out of bounds");
        return BLOCK_ERR_BOUNDS;
    }

    /* Route partition sub-device to parent */
    if (dev->parent) {
        return block_device_read(dev->parent, dev->partition_offset_lba + lba, count, buffer);
    }

    if (!dev->read) {
        return BLOCK_ERR_INVAL;
    }

    return dev->read(dev, lba, count, buffer);
}

int block_device_write(block_device_t *dev, uint64_t lba, uint32_t count, const void *buffer) {
    if (!dev || !buffer || count == 0) {
        return BLOCK_ERR_INVAL;
    }

    if (!dev->present) {
        return BLOCK_ERR_NODEV;
    }

    if (dev->read_only) {
        kwarn("block_device_write: device is read-only");
        return BLOCK_ERR_ROFS;
    }

    /* Integer overflow check */
    if (lba + count < lba) {
        kwarn("block_device_write: LBA overflow");
        return BLOCK_ERR_BOUNDS;
    }

    /* Capacity boundary check */
    if (dev->capacity_sectors > 0 && lba + count > dev->capacity_sectors) {
        kwarn("block_device_write: LBA out of bounds");
        return BLOCK_ERR_BOUNDS;
    }

    /* Route partition sub-device to parent */
    if (dev->parent) {
        return block_device_write(dev->parent, dev->partition_offset_lba + lba, count, buffer);
    }

    if (!dev->write) {
        return BLOCK_ERR_INVAL;
    }

    return dev->write(dev, lba, count, buffer);
}

int block_device_flush(block_device_t *dev) {
    if (!dev) return BLOCK_ERR_INVAL;
    if (!dev->present) return BLOCK_ERR_NODEV;

    if (dev->parent) {
        return block_device_flush(dev->parent);
    }

    if (dev->flush) {
        return dev->flush(dev);
    }

    return BLOCK_OK;
}
