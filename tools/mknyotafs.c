/* =============================================================================
 * Nyota OS — Host NyotaFS Image Builder (mknyotafs)
 * Creates a raw formatted NyotaFS disk image and populates it from a directory.
 * =========================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <dirent.h>

#define NYOTA_FS_MAGIC          0x4E594F54ULL  /* "NYOT" */
#define NYOTA_FS_VERSION        1
#define NYOTA_BLOCK_SIZE        1024
#define NYOTA_SECTOR_SIZE       512

#define NYOTA_INODE_DIRECT_BLOCKS 8
#define NYOTA_INODE_SIZE          128
#define NYOTA_INODES_PER_BLOCK    (NYOTA_BLOCK_SIZE / NYOTA_INODE_SIZE)

#define NYOTA_MODE_FILE         0x8000
#define NYOTA_MODE_DIR          0x4000
#define NYOTA_MODE_DEV          0x2000

#define NYOTA_FT_FILE           1
#define NYOTA_FT_DIR            2
#define NYOTA_FT_DEV            3

#define NYOTA_NAME_MAX          54
#define NYOTA_DIRENTS_PER_BLOCK (NYOTA_BLOCK_SIZE / sizeof(nyota_dirent_t))

#pragma pack(push, 1)

typedef struct {
    uint32_t magic;
    uint32_t version;

    uint64_t total_blocks;
    uint64_t free_blocks;

    uint64_t total_inodes;
    uint64_t free_inodes;

    uint32_t block_size;

    uint64_t block_bitmap_block;
    uint64_t block_bitmap_blocks;

    uint64_t inode_bitmap_block;
    uint64_t inode_bitmap_blocks;

    uint64_t inode_table_start;
    uint64_t inode_table_blocks;

    uint64_t data_block_start;

    uint64_t root_inode;

    uint8_t  padding[432];
} nyota_superblock_t;

typedef struct {
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t link_count;

    uint64_t size;
    uint64_t created;
    uint64_t modified;

    uint64_t direct_blocks[NYOTA_INODE_DIRECT_BLOCKS];
    uint64_t indirect_block;

    uint32_t dev_major;
    uint32_t dev_minor;

    uint8_t  reserved[8];
} nyota_inode_t;

typedef struct {
    uint64_t inode;
    uint8_t  type;
    uint8_t  name_len;
    char     name[NYOTA_NAME_MAX];
} nyota_dirent_t;

#pragma pack(pop)

static uint8_t *disk_image = NULL;
static size_t disk_size = 0;
static uint64_t total_blocks = 0;

static uint64_t next_free_block = 0;
static uint64_t next_free_inode = 1;

static nyota_superblock_t *sb = NULL;
static uint8_t *block_bitmap = NULL;
static uint8_t *inode_bitmap = NULL;
static nyota_inode_t *inode_table = NULL;

static int file_count = 0;
static int dir_count = 0;

static void set_bitmap(uint8_t *bitmap, uint64_t index) {
    bitmap[index / 8] |= (1 << (index % 8));
}

static uint64_t alloc_block(void) {
    if (next_free_block >= total_blocks) {
        fprintf(stderr, "Error: out of disk blocks\n");
        exit(1);
    }
    uint64_t blk = next_free_block++;
    uint64_t data_idx = blk - sb->data_block_start;
    set_bitmap(block_bitmap, data_idx);
    sb->free_blocks--;
    memset(disk_image + (blk * NYOTA_BLOCK_SIZE), 0, NYOTA_BLOCK_SIZE);
    return blk;
}

static uint64_t alloc_inode(uint32_t mode) {
    if (next_free_inode >= sb->total_inodes) {
        fprintf(stderr, "Error: out of inodes\n");
        exit(1);
    }
    uint64_t ino = next_free_inode++;
    set_bitmap(inode_bitmap, ino);
    sb->free_inodes--;

    nyota_inode_t *node = &inode_table[ino];
    memset(node, 0, sizeof(nyota_inode_t));
    node->mode = mode;
    node->link_count = 1;
    return ino;
}

static void add_dir_entry(uint64_t dir_inode, uint64_t child_inode, const char *name, uint8_t type) {
    nyota_inode_t *dir = &inode_table[dir_inode];

    /* Search existing direct blocks for a free slot */
    for (int b = 0; b < NYOTA_INODE_DIRECT_BLOCKS; b++) {
        if (dir->direct_blocks[b] == 0) {
            dir->direct_blocks[b] = alloc_block();
        }
        nyota_dirent_t *entries = (nyota_dirent_t *)(disk_image + (dir->direct_blocks[b] * NYOTA_BLOCK_SIZE));
        for (size_t i = 0; i < NYOTA_DIRENTS_PER_BLOCK; i++) {
            if (entries[i].inode == 0) {
                entries[i].inode = child_inode;
                entries[i].type = type;
                size_t len = strlen(name);
                if (len >= NYOTA_NAME_MAX) len = NYOTA_NAME_MAX - 1;
                entries[i].name_len = (uint8_t)len;
                memcpy(entries[i].name, name, len);
                entries[i].name[len] = '\0';
                dir->size += sizeof(nyota_dirent_t);
                return;
            }
        }
    }

    fprintf(stderr, "Error: directory too full\n");
    exit(1);
}

static void populate_file(uint64_t inode_num, const char *host_path) {
    FILE *fp = fopen(host_path, "rb");
    if (!fp) {
        fprintf(stderr, "Warning: failed to open host file %s\n", host_path);
        return;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    nyota_inode_t *node = &inode_table[inode_num];
    node->size = (uint64_t)size;

    uint32_t num_blocks = (uint32_t)((size + NYOTA_BLOCK_SIZE - 1) / NYOTA_BLOCK_SIZE);
    uint64_t *indirect_data = NULL;

    for (uint32_t b = 0; b < num_blocks; b++) {
        uint64_t blk = alloc_block();
        if (b < NYOTA_INODE_DIRECT_BLOCKS) {
            node->direct_blocks[b] = blk;
        } else {
            if (node->indirect_block == 0) {
                node->indirect_block = alloc_block();
                indirect_data = (uint64_t *)(disk_image + (node->indirect_block * NYOTA_BLOCK_SIZE));
            }
            uint32_t ind_idx = b - NYOTA_INODE_DIRECT_BLOCKS;
            if (ind_idx < (NYOTA_BLOCK_SIZE / sizeof(uint64_t))) {
                indirect_data[ind_idx] = blk;
            } else {
                fprintf(stderr, "Error: file %s exceeds max single-indirect size\n", host_path);
                fclose(fp);
                exit(1);
            }
        }

        size_t read_bytes = fread(disk_image + (blk * NYOTA_BLOCK_SIZE), 1, NYOTA_BLOCK_SIZE, fp);
        (void)read_bytes;
    }

    fclose(fp);
    file_count++;
}

static void scan_directory(uint64_t dir_inode, uint64_t parent_inode, const char *host_dir) {
    DIR *d = opendir(host_dir);
    if (!d) {
        fprintf(stderr, "Warning: unable to open host dir %s\n", host_dir);
        return;
    }

    /* Add . and .. */
    add_dir_entry(dir_inode, dir_inode, ".", NYOTA_FT_DIR);
    add_dir_entry(dir_inode, parent_inode, "..", NYOTA_FT_DIR);
    dir_count++;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", host_dir, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            uint64_t child_inode = alloc_inode(NYOTA_MODE_DIR | 0755);
            add_dir_entry(dir_inode, child_inode, entry->d_name, NYOTA_FT_DIR);
            scan_directory(child_inode, dir_inode, full_path);
        } else if (S_ISREG(st.st_mode)) {
            uint64_t child_inode = alloc_inode(NYOTA_MODE_FILE | 0755);
            add_dir_entry(dir_inode, child_inode, entry->d_name, NYOTA_FT_FILE);
            populate_file(child_inode, full_path);
        }
    }

    closedir(d);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <source_dir> <output_image> [size_in_mb]\n", argv[0]);
        return 1;
    }

    const char *source_dir = argv[1];
    const char *output_img = argv[2];
    int size_mb = (argc >= 4) ? atoi(argv[3]) : 16;
    if (size_mb < 2) size_mb = 16;

    disk_size = (size_t)size_mb * 1024 * 1024;
    total_blocks = disk_size / NYOTA_BLOCK_SIZE;

    disk_image = (uint8_t *)calloc(1, disk_size);
    if (!disk_image) {
        fprintf(stderr, "Failed to allocate memory for disk image\n");
        return 1;
    }

    /* Layout:
     * Block 0: Superblock
     * Blocks 1..2: Block bitmap (2 blocks = 16384 bits = 16MB)
     * Block 3: Inode bitmap (1 block = 8192 bits)
     * Blocks 4..131: Inode table (128 blocks * 8 inodes/block = 1024 inodes)
     * Blocks 132..total_blocks-1: Data blocks
     */
    sb = (nyota_superblock_t *)disk_image;
    sb->magic = (uint32_t)NYOTA_FS_MAGIC;
    sb->version = NYOTA_FS_VERSION;
    sb->total_blocks = total_blocks;
    sb->block_size = NYOTA_BLOCK_SIZE;

    sb->block_bitmap_block = 1;
    sb->block_bitmap_blocks = (total_blocks + (NYOTA_BLOCK_SIZE * 8) - 1) / (NYOTA_BLOCK_SIZE * 8);

    sb->inode_bitmap_block = sb->block_bitmap_block + sb->block_bitmap_blocks;
    sb->inode_bitmap_blocks = 1;

    sb->inode_table_start = sb->inode_bitmap_block + sb->inode_bitmap_blocks;
    sb->inode_table_blocks = 128; /* 1024 inodes */
    sb->total_inodes = sb->inode_table_blocks * NYOTA_INODES_PER_BLOCK;

    sb->data_block_start = sb->inode_table_start + sb->inode_table_blocks;
    sb->free_blocks = total_blocks - sb->data_block_start;
    sb->free_inodes = sb->total_inodes - 1; /* Inode 0 reserved */

    sb->root_inode = 1;

    block_bitmap = disk_image + (sb->block_bitmap_block * NYOTA_BLOCK_SIZE);
    inode_bitmap = disk_image + (sb->inode_bitmap_block * NYOTA_BLOCK_SIZE);
    inode_table  = (nyota_inode_t *)(disk_image + (sb->inode_table_start * NYOTA_BLOCK_SIZE));

    /* Mark inode 0 as used */
    set_bitmap(inode_bitmap, 0);

    next_free_block = sb->data_block_start;
    next_free_inode = 1;

    printf("[MKNYOTAFS] Creating %s (%d MB, %llu blocks, %llu inodes)\n",
           output_img, size_mb, (unsigned long long)total_blocks, (unsigned long long)sb->total_inodes);

    /* Allocate root inode 1 */
    uint64_t root_ino = alloc_inode(NYOTA_MODE_DIR | 0755);

    /* Recursively scan and populate from source directory */
    scan_directory(root_ino, root_ino, source_dir);

    /* Write out the image to file */
    FILE *out = fopen(output_img, "wb");
    if (!out) {
        fprintf(stderr, "Failed to open output image %s for writing\n", output_img);
        free(disk_image);
        return 1;
    }

    size_t written = fwrite(disk_image, 1, disk_size, out);
    fclose(out);

    if (written != disk_size) {
        fprintf(stderr, "Failed to write complete disk image\n");
        free(disk_image);
        return 1;
    }

    printf("[MKNYOTAFS] Populated %d files, %d directories\n", file_count, dir_count);
    printf("[MKNYOTAFS] Free blocks remaining: %llu / %llu\n",
           (unsigned long long)sb->free_blocks, (unsigned long long)(total_blocks - sb->data_block_start));
    printf("[MKNYOTAFS] Done successfully.\n");

    free(disk_image);
    return 0;
}
