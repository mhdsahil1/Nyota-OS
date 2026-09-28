#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define NYOTA_BLOCK_SIZE 1024
#define NYOTA_INODE_SIZE 128

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
} __attribute__((packed)) sb_t;

typedef struct {
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t link_count;
    uint64_t size;
    uint64_t created;
    uint64_t modified;
    uint64_t direct_blocks[8];
    uint64_t indirect_block;
    uint32_t dev_major;
    uint32_t dev_minor;
    uint8_t  reserved[8];
} __attribute__((packed)) inode_t;

typedef struct {
    uint64_t inode;
    uint8_t  type;
    uint8_t  name_len;
    char     name[54];
} __attribute__((packed)) dirent_t;

int main(void) {
    FILE *fp = fopen("build/nyota-data.img", "rb");
    if (!fp) return 1;

    sb_t sb;
    fread(&sb, 1, sizeof(sb), fp);

    /* Inode 1 is / */
    fseek(fp, (sb.inode_table_start * NYOTA_BLOCK_SIZE) + (1 * NYOTA_INODE_SIZE), SEEK_SET);
    inode_t root_node;
    fread(&root_node, 1, sizeof(root_node), fp);

    printf("Root directory (mode=0x%x, size=%llu, block=%llu):\n",
           root_node.mode, (unsigned long long)root_node.size, (unsigned long long)root_node.direct_blocks[0]);
    fseek(fp, root_node.direct_blocks[0] * NYOTA_BLOCK_SIZE, SEEK_SET);
    dirent_t entries[16];
    fread(entries, sizeof(dirent_t), 16, fp);
    for (int i = 0; i < 16; i++) {
        if (entries[i].inode != 0) {
            printf("  [%d] inode=%llu, type=%u, name='%s'\n",
                   i, (unsigned long long)entries[i].inode, entries[i].type, entries[i].name);
        }
    }

    fclose(fp);
    return 0;
}
