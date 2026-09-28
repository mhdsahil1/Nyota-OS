/* =============================================================================
 * Nyota OS — Generic Block Device Subsystem Implementation
 * Manages registered block devices and routes sector read/write operations.
 * =========================================================================== */

#include "storage/block.h"
#include "memory.h"
#include "vga.h"
#include "kernel.h"

static block_device_t *devices[MAX_BLOCK_DEVICES] = {0};
static size_t device_count = 0;

void block_device_init(void) {
    for (size_t i = 0; i < MAX_BLOCK_DEVICES; i++) {
        devices[i] = NULL;
    }
    device_count = 0;
}

int block_device_register(block_device_t *dev) {
    if (!dev || !dev->read) {
        return BLOCK_ERR_INVAL;
    }

    if (device_count >= MAX_BLOCK_DEVICES) {
        kwarn("block_device_register: device table full");
        return BLOCK_ERR_NODEV;
    }

    dev->id = (uint32_t)device_count;
    devices[device_count++] = dev;

    return BLOCK_OK;
}

block_device_t *block_device_get(uint32_t id) {
    if (id < device_count) {
        return devices[id];
    }
    return NULL;
}

block_device_t *block_device_find_by_name(const char *name) {
    if (!name) return NULL;

    for (size_t i = 0; i < device_count; i++) {
        if (devices[i] && strcmp(devices[i]->name, name) == 0) {
            return devices[i];
        }
    }
    return NULL;
}

size_t block_device_count(void) {
    return device_count;
}

int block_device_read(block_device_t *dev, uint64_t sector, uint32_t count, void *buffer) {
    if (!dev || !dev->read || !buffer || count == 0) {
        return BLOCK_ERR_INVAL;
    }

    if (sector + count > dev->sector_count && dev->sector_count > 0) {
        kwarn("block_device_read: LBA out of bounds");
        return BLOCK_ERR_INVAL;
    }

    return dev->read(sector, count, buffer);
}

int block_device_write(block_device_t *dev, uint64_t sector, uint32_t count, const void *buffer) {
    if (!dev || !dev->write || !buffer || count == 0) {
        return BLOCK_ERR_INVAL;
    }

    if (sector + count > dev->sector_count && dev->sector_count > 0) {
        kwarn("block_device_write: LBA out of bounds");
        return BLOCK_ERR_INVAL;
    }

    return dev->write(sector, count, buffer);
}
