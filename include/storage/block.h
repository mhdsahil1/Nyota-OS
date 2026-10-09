/* =============================================================================
 * Nyota OS — Unified Block Device Subsystem Interface (Phase 12)
 * Hardware-agnostic block storage abstraction supporting ATA, AHCI, NVMe,
 * and partition discovery.
 * =========================================================================== */

#ifndef NYOTA_STORAGE_BLOCK_H
#define NYOTA_STORAGE_BLOCK_H

#include "types.h"

#define BLOCK_DEV_NAME_MAX  32
#define MAX_BLOCK_DEVICES   32

/* Status Codes */
#define BLOCK_OK            0
#define BLOCK_ERR_INVAL    -1
#define BLOCK_ERR_IO       -2
#define BLOCK_ERR_NODEV    -3
#define BLOCK_ERR_ROFS     -4
#define BLOCK_ERR_BOUNDS   -5

struct block_device;
typedef struct block_device block_device_t;

/* Unified Block Device Abstraction */
struct block_device {
    char name[BLOCK_DEV_NAME_MAX];
    uint32_t id;

    uint64_t capacity_sectors;
    uint64_t sector_count;        /* Kept for backward compatibility */
    uint32_t sector_size;

    bool read_only;
    bool present;

    int (*read)(struct block_device *dev,
                uint64_t lba,
                uint32_t count,
                void *buffer);

    int (*write)(struct block_device *dev,
                 uint64_t lba,
                 uint32_t count,
                 const void *buffer);

    int (*flush)(struct block_device *dev);

    void *private_data;

    /* Driver & Model metadata */
    char type_str[16];            /* "ATA", "AHCI", "NVME", "PARTITION" */
    char model[48];               /* Hardware model or device label */

    /* Partition relationship (if device is a partition sub-device) */
    struct block_device *parent;
    uint64_t partition_offset_lba;
    uint8_t  partition_num;
    uint8_t  partition_type;
};

/* Block Device Manager APIs */
void block_device_init(void);
int block_device_register(block_device_t *dev);
int block_device_unregister(block_device_t *dev);
block_device_t *block_device_get(uint32_t id);
block_device_t *block_device_find_by_name(const char *name);
size_t block_device_count(void);

/* Generic I/O functions with boundary & read-only validation */
int block_device_read(block_device_t *dev, uint64_t lba, uint32_t count, void *buffer);
int block_device_write(block_device_t *dev, uint64_t lba, uint32_t count, const void *buffer);
int block_device_flush(block_device_t *dev);

#endif /* NYOTA_STORAGE_BLOCK_H */
