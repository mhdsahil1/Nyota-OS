/* =============================================================================
 * Nyota OS — Block Device Subsystem Interface (Phase 6)
 * Hardware-agnostic block storage abstraction.
 * =========================================================================== */

#ifndef NYOTA_STORAGE_BLOCK_H
#define NYOTA_STORAGE_BLOCK_H

#include "types.h"

#define BLOCK_DEV_NAME_MAX  16
#define MAX_BLOCK_DEVICES   8

#define BLOCK_OK            0
#define BLOCK_ERR_INVAL    -1
#define BLOCK_ERR_IO       -2
#define BLOCK_ERR_NODEV    -3

typedef struct block_device {
    char name[BLOCK_DEV_NAME_MAX];
    uint32_t id;

    uint32_t sector_size;
    uint64_t sector_count;

    int (*read)(uint64_t sector, uint32_t count, void *buffer);
    int (*write)(uint64_t sector, uint32_t count, const void *buffer);

    void *private_data;
} block_device_t;

/* Block Device Manager APIs */
void block_device_init(void);
int block_device_register(block_device_t *dev);
block_device_t *block_device_get(uint32_t id);
block_device_t *block_device_find_by_name(const char *name);
size_t block_device_count(void);

/* Generic I/O functions */
int block_device_read(block_device_t *dev, uint64_t sector, uint32_t count, void *buffer);
int block_device_write(block_device_t *dev, uint64_t sector, uint32_t count, const void *buffer);

#endif /* NYOTA_STORAGE_BLOCK_H */
