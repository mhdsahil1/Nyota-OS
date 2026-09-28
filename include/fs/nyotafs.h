/* =============================================================================
 * Nyota OS — NyotaFS On-Disk Structures & Constants (Phase 6)
 * Native filesystem definition for persistent block storage.
 * =========================================================================== */

#ifndef NYOTA_FS_NYOTAFS_H
#define NYOTA_FS_NYOTAFS_H

#include "types.h"
#include "storage/block.h"

#define NYOTA_FS_MAGIC          0x4E594F54ULL  /* "NYOT" */
#define NYOTA_FS_VERSION        1
#define NYOTA_BLOCK_SIZE        1024
#define NYOTA_SECTOR_SIZE       512
#define NYOTA_SECTORS_PER_BLOCK (NYOTA_BLOCK_SIZE / NYOTA_SECTOR_SIZE)

#define NYOTA_INODE_DIRECT_BLOCKS 8
#define NYOTA_INODE_SIZE          128
#define NYOTA_INODES_PER_BLOCK    (NYOTA_BLOCK_SIZE / NYOTA_INODE_SIZE)

/* File Types & Modes */
#define NYOTA_MODE_TYPE_MASK    0xF000
#define NYOTA_MODE_FILE         0x8000
#define NYOTA_MODE_DIR          0x4000
#define NYOTA_MODE_DEV          0x2000

#define NYOTA_PERM_R            0x04
#define NYOTA_PERM_W            0x02
#define NYOTA_PERM_X            0x01
#define NYOTA_PERM_RWX          0x07

/* Dirent Types */
#define NYOTA_FT_UNKNOWN        0
#define NYOTA_FT_FILE           1
#define NYOTA_FT_DIR            2
#define NYOTA_FT_DEV            3

#define NYOTA_NAME_MAX          54
#define NYOTA_DIRENTS_PER_BLOCK (NYOTA_BLOCK_SIZE / sizeof(nyota_dirent_t))

/* ── Superblock Structure (Offset 0 of disk, Block 0) ─────────────────────── */
typedef struct {
    uint32_t magic;              /* NYOTA_FS_MAGIC */
    uint32_t version;            /* NYOTA_FS_VERSION */

    uint64_t total_blocks;       /* Total blocks in filesystem */
    uint64_t free_blocks;        /* Available data blocks */

    uint64_t total_inodes;       /* Total inodes in table */
    uint64_t free_inodes;        /* Available inodes */

    uint32_t block_size;         /* Block size in bytes (1024) */

    uint64_t block_bitmap_block; /* Starting block of block bitmap */
    uint64_t block_bitmap_blocks;/* Number of blocks in block bitmap */

    uint64_t inode_bitmap_block; /* Starting block of inode bitmap */
    uint64_t inode_bitmap_blocks;/* Number of blocks in inode bitmap */

    uint64_t inode_table_start;  /* Starting block of inode table */
    uint64_t inode_table_blocks; /* Number of blocks in inode table */

    uint64_t data_block_start;   /* Starting block of data area */

    uint64_t root_inode;         /* Root directory inode number (1) */

    uint8_t  padding[432];       /* Pad out to 512 bytes */
} __attribute__((packed)) nyota_superblock_t;

/* ── Inode Structure (128 bytes) ──────────────────────────────────────────── */
typedef struct {
    uint32_t mode;               /* File mode (type and permissions) */
    uint32_t uid;                /* Owner user ID */
    uint32_t gid;                /* Owner group ID */
    uint32_t link_count;         /* Hard link count */

    uint64_t size;               /* File size in bytes */
    uint64_t created;            /* Creation timestamp */
    uint64_t modified;           /* Last modification timestamp */

    uint64_t direct_blocks[NYOTA_INODE_DIRECT_BLOCKS]; /* Direct data block indices (64 bytes) */
    uint64_t indirect_block;     /* Indirect block index (8 bytes) */

    uint32_t dev_major;          /* Major number for device files */
    uint32_t dev_minor;          /* Minor number for device files */

    uint8_t  reserved[8];        /* Exactly 128 bytes total */
} __attribute__((packed)) nyota_inode_t;

/* ── Directory Entry Structure (64 bytes) ─────────────────────────────────── */
typedef struct {
    uint64_t inode;              /* Inode number (0 = free entry) */
    uint8_t  type;               /* File type (NYOTA_FT_*) */
    uint8_t  name_len;           /* Filename length */
    char     name[NYOTA_NAME_MAX]; /* Null-terminated filename */
} __attribute__((packed)) nyota_dirent_t;

/* ── In-Memory Filesystem State ───────────────────────────────────────────── */
typedef struct nyota_fs {
    block_device_t    *bdev;
    nyota_superblock_t sb;
    uint8_t           *block_bitmap;
    uint8_t           *inode_bitmap;
    bool               mounted;
} nyota_fs_t;

/* APIs */
int nyotafs_mount(block_device_t *bdev, nyota_fs_t *fs);
int nyotafs_read_inode(nyota_fs_t *fs, uint64_t inode_num, nyota_inode_t *inode);
int nyotafs_write_inode(nyota_fs_t *fs, uint64_t inode_num, const nyota_inode_t *inode);

int nyotafs_alloc_block(nyota_fs_t *fs, uint64_t *out_block);
int nyotafs_free_block(nyota_fs_t *fs, uint64_t block);

int nyotafs_alloc_inode(nyota_fs_t *fs, uint64_t *out_inode);
int nyotafs_free_inode(nyota_fs_t *fs, uint64_t inode_num);

int nyotafs_read_block(nyota_fs_t *fs, uint64_t block, void *buf);
int nyotafs_write_block(nyota_fs_t *fs, uint64_t block, const void *buf);

int nyotafs_read_file(nyota_fs_t *fs, const nyota_inode_t *inode, uint64_t offset, void *buf, size_t count);
int nyotafs_write_file(nyota_fs_t *fs, uint64_t inode_num, nyota_inode_t *inode, uint64_t offset, const void *buf, size_t count);

int nyotafs_lookup(nyota_fs_t *fs, uint64_t dir_inode_num, const char *name, uint64_t *out_inode);
int nyotafs_create_entry(nyota_fs_t *fs, uint64_t dir_inode_num, const char *name, uint64_t child_inode, uint8_t type);
int nyotafs_delete_entry(nyota_fs_t *fs, uint64_t dir_inode_num, const char *name);

int nyotafs_resolve_path(nyota_fs_t *fs, const char *path, uint64_t *out_inode);
int nyotafs_create_file(nyota_fs_t *fs, const char *path, uint32_t mode, uint64_t *out_inode);
int nyotafs_mkdir(nyota_fs_t *fs, const char *path, uint32_t mode, uint64_t *out_inode);

#endif /* NYOTA_FS_NYOTAFS_H */
