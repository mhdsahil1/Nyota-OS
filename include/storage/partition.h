/* =============================================================================
 * Nyota OS — Partition Discovery Subsystem Interface (Phase 12)
 * Discovers and validates MBR and GPT partition tables on block devices.
 * =========================================================================== */

#ifndef NYOTA_STORAGE_PARTITION_H
#define NYOTA_STORAGE_PARTITION_H

#include "types.h"
#include "storage/block.h"

#define MBR_SIGNATURE           0xAA55
#define MBR_PARTITION_COUNT     4
#define GPT_SIGNATURE           "EFI PART"
#define MAX_PARTITIONS_TOTAL    32

/* MBR Partition Entry (16 bytes) */
typedef struct __attribute__((packed)) {
    uint8_t  status;            /* 0x80 = Active / Bootable */
    uint8_t  chs_start[3];
    uint8_t  type;              /* Partition type (0x00=Empty, 0xEE=GPT, 0x83=Linux, etc.) */
    uint8_t  chs_end[3];
    uint32_t lba_start;         /* Starting LBA */
    uint32_t sector_count;      /* Number of sectors */
} mbr_entry_t;

/* MBR Sector Layout (512 bytes) */
typedef struct __attribute__((packed)) {
    uint8_t     bootstrap[446];
    mbr_entry_t entries[MBR_PARTITION_COUNT];
    uint16_t    signature;      /* 0xAA55 */
} mbr_sector_t;

/* GPT Header (92 bytes minimum) */
typedef struct __attribute__((packed)) {
    char     signature[8];      /* "EFI PART" */
    uint32_t revision;          /* 0x00010000 (v1.0) */
    uint32_t header_size;       /* Typically 92 */
    uint32_t header_crc32;      /* CRC32 of header with this field zeroed */
    uint32_t reserved;
    uint64_t current_lba;       /* LBA of this header (usually 1) */
    uint64_t backup_lba;        /* LBA of backup header */
    uint64_t first_usable_lba;  /* First usable block for partitions */
    uint64_t last_usable_lba;   /* Last usable block for partitions */
    uint8_t  disk_guid[16];
    uint64_t partition_entries_lba; /* Starting LBA of partition entries array */
    uint32_t num_partition_entries; /* Typically 128 */
    uint32_t sizeof_partition_entry;/* Typically 128 */
    uint32_t partition_array_crc32; /* CRC32 of partition entries array */
} gpt_header_t;

/* GPT Partition Entry (128 bytes) */
typedef struct __attribute__((packed)) {
    uint8_t  type_guid[16];     /* Partition Type GUID (all 0 = unused) */
    uint8_t  unique_guid[16];   /* Unique Partition GUID */
    uint64_t starting_lba;      /* Starting LBA */
    uint64_t ending_lba;        /* Ending LBA (inclusive) */
    uint64_t attributes;        /* Attribute flags */
    uint16_t name[36];          /* Partition name (UTF-16LE) */
} gpt_entry_t;

/* Partition APIs */
void partition_init(void);
void partition_scan_all(void);
int  partition_scan(block_device_t *disk);
uint32_t partition_crc32(const void *data, size_t length);

#endif /* NYOTA_STORAGE_PARTITION_H */
