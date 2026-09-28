/* =============================================================================
 * Nyota OS — NyotaFS Native Filesystem Implementation (Phase 6)
 * Handles superblock, block/inode bitmaps, inode table, directory walking,
 * and persistent file read/write operations.
 * =========================================================================== */

#include "fs/nyotafs.h"
#include "heap.h"
#include "memory.h"
#include "vga.h"
#include "kernel.h"

/* ── Block I/O Operations ─────────────────────────────────────────────────── */

int nyotafs_read_block(nyota_fs_t *fs, uint64_t block, void *buf) {
    if (!fs || !fs->bdev || !buf) return -1;
    uint64_t sector = block * NYOTA_SECTORS_PER_BLOCK;
    return block_device_read(fs->bdev, sector, NYOTA_SECTORS_PER_BLOCK, buf);
}

int nyotafs_write_block(nyota_fs_t *fs, uint64_t block, const void *buf) {
    if (!fs || !fs->bdev || !buf) return -1;
    uint64_t sector = block * NYOTA_SECTORS_PER_BLOCK;
    return block_device_write(fs->bdev, sector, NYOTA_SECTORS_PER_BLOCK, buf);
}

/* ── Inode Table Access ───────────────────────────────────────────────────── */

int nyotafs_read_inode(nyota_fs_t *fs, uint64_t inode_num, nyota_inode_t *inode) {
    if (!fs || (uint64_t)fs < 0x100000 || (uint64_t)fs >= 0x8000000 || !inode || inode_num == 0 || inode_num >= fs->sb.total_inodes) {
        return -1;
    }

    uint64_t block_idx = fs->sb.inode_table_start + (inode_num / NYOTA_INODES_PER_BLOCK);
    uint32_t offset = (uint32_t)((inode_num % NYOTA_INODES_PER_BLOCK) * sizeof(nyota_inode_t));

    uint8_t blk_buf[NYOTA_BLOCK_SIZE];
    if (nyotafs_read_block(fs, block_idx, blk_buf) != 0) {
        return -1;
    }

    memcpy(inode, blk_buf + offset, sizeof(nyota_inode_t));
    return 0;
}

int nyotafs_write_inode(nyota_fs_t *fs, uint64_t inode_num, const nyota_inode_t *inode) {
    if (!fs || (uint64_t)fs < 0x100000 || (uint64_t)fs >= 0x8000000 || !inode || inode_num == 0 || inode_num >= fs->sb.total_inodes) {
        return -1;
    }


    uint64_t block_idx = fs->sb.inode_table_start + (inode_num / NYOTA_INODES_PER_BLOCK);
    uint32_t offset = (uint32_t)((inode_num % NYOTA_INODES_PER_BLOCK) * sizeof(nyota_inode_t));

    uint8_t blk_buf[NYOTA_BLOCK_SIZE];
    if (nyotafs_read_block(fs, block_idx, blk_buf) != 0) {
        return -1;
    }

    memcpy(blk_buf + offset, inode, sizeof(nyota_inode_t));
    return nyotafs_write_block(fs, block_idx, blk_buf);
}

/* ── Allocation Helpers ───────────────────────────────────────────────────── */

int nyotafs_alloc_block(nyota_fs_t *fs, uint64_t *out_block) {
    if (!fs || !out_block || fs->sb.free_blocks == 0) return -1;

    uint8_t buf[NYOTA_BLOCK_SIZE];
    for (uint64_t bb = 0; bb < fs->sb.block_bitmap_blocks; bb++) {
        uint64_t bm_block = fs->sb.block_bitmap_block + bb;
        if (nyotafs_read_block(fs, bm_block, buf) != 0) return -1;

        for (uint32_t byte_idx = 0; byte_idx < NYOTA_BLOCK_SIZE; byte_idx++) {
            if (buf[byte_idx] != 0xFF) {
                for (int bit = 0; bit < 8; bit++) {
                    if (!(buf[byte_idx] & (1 << bit))) {
                        buf[byte_idx] |= (1 << bit);
                        nyotafs_write_block(fs, bm_block, buf);

                        uint64_t data_idx = (bb * NYOTA_BLOCK_SIZE * 8) + (byte_idx * 8) + bit;
                        uint64_t alloc_blk = fs->sb.data_block_start + data_idx;

                        /* Zero out newly allocated block */
                        uint8_t zero_buf[NYOTA_BLOCK_SIZE];
                        memset(zero_buf, 0, sizeof(zero_buf));
                        nyotafs_write_block(fs, alloc_blk, zero_buf);

                        fs->sb.free_blocks--;
                        *out_block = alloc_blk;
                        return 0;
                    }
                }
            }
        }
    }
    return -1;
}

int nyotafs_free_block(nyota_fs_t *fs, uint64_t block) {
    if (!fs || block < fs->sb.data_block_start) return -1;

    uint64_t data_idx = block - fs->sb.data_block_start;
    uint64_t bb = data_idx / (NYOTA_BLOCK_SIZE * 8);
    uint32_t rem = data_idx % (NYOTA_BLOCK_SIZE * 8);
    uint32_t byte_idx = rem / 8;
    int bit = rem % 8;

    uint8_t buf[NYOTA_BLOCK_SIZE];
    uint64_t bm_block = fs->sb.block_bitmap_block + bb;
    if (nyotafs_read_block(fs, bm_block, buf) != 0) return -1;

    buf[byte_idx] &= ~(1 << bit);
    if (nyotafs_write_block(fs, bm_block, buf) != 0) return -1;

    fs->sb.free_blocks++;
    return 0;
}

int nyotafs_alloc_inode(nyota_fs_t *fs, uint64_t *out_inode) {
    if (!fs || !out_inode || fs->sb.free_inodes == 0) return -1;

    uint8_t buf[NYOTA_BLOCK_SIZE];
    for (uint64_t ib = 0; ib < fs->sb.inode_bitmap_blocks; ib++) {
        uint64_t bm_block = fs->sb.inode_bitmap_block + ib;
        if (nyotafs_read_block(fs, bm_block, buf) != 0) return -1;

        for (uint32_t byte_idx = 0; byte_idx < NYOTA_BLOCK_SIZE; byte_idx++) {
            if (buf[byte_idx] != 0xFF) {
                for (int bit = 0; bit < 8; bit++) {
                    uint64_t ino = (ib * NYOTA_BLOCK_SIZE * 8) + (byte_idx * 8) + bit;
                    if (ino == 0) continue; /* Inode 0 reserved */
                    if (ino >= fs->sb.total_inodes) return -1;

                    if (!(buf[byte_idx] & (1 << bit))) {
                        buf[byte_idx] |= (1 << bit);
                        nyotafs_write_block(fs, bm_block, buf);

                        fs->sb.free_inodes--;
                        *out_inode = ino;
                        return 0;
                    }
                }
            }
        }
    }
    return -1;
}

int nyotafs_free_inode(nyota_fs_t *fs, uint64_t inode_num) {
    if (!fs || inode_num == 0 || inode_num >= fs->sb.total_inodes) return -1;

    uint64_t ib = inode_num / (NYOTA_BLOCK_SIZE * 8);
    uint32_t rem = inode_num % (NYOTA_BLOCK_SIZE * 8);
    uint32_t byte_idx = rem / 8;
    int bit = rem % 8;

    uint8_t buf[NYOTA_BLOCK_SIZE];
    uint64_t bm_block = fs->sb.inode_bitmap_block + ib;
    if (nyotafs_read_block(fs, bm_block, buf) != 0) return -1;

    buf[byte_idx] &= ~(1 << bit);
    if (nyotafs_write_block(fs, bm_block, buf) != 0) return -1;

    fs->sb.free_inodes++;
    return 0;
}

/* ── File Read & Write ────────────────────────────────────────────────────── */

int nyotafs_read_file(nyota_fs_t *fs, const nyota_inode_t *inode, uint64_t offset, void *buf, size_t count) {
    if (!fs || !inode || !buf) return -1;
    if (offset >= inode->size) return 0; /* EOF */

    if (offset + count > inode->size) {
        count = (size_t)(inode->size - offset);
    }

    uint8_t *dest = (uint8_t *)buf;
    size_t bytes_read = 0;

    uint8_t block_buf[NYOTA_BLOCK_SIZE];
    uint64_t indirect_buf[NYOTA_BLOCK_SIZE / sizeof(uint64_t)];
    bool indirect_loaded = false;

    while (bytes_read < count) {
        uint64_t curr_pos = offset + bytes_read;
        uint32_t blk_index = (uint32_t)(curr_pos / NYOTA_BLOCK_SIZE);
        uint32_t blk_offset = (uint32_t)(curr_pos % NYOTA_BLOCK_SIZE);
        size_t chunk = NYOTA_BLOCK_SIZE - blk_offset;
        if (chunk > (count - bytes_read)) {
            chunk = count - bytes_read;
        }

        uint64_t phys_blk = 0;
        if (blk_index < NYOTA_INODE_DIRECT_BLOCKS) {
            phys_blk = inode->direct_blocks[blk_index];
        } else {
            uint32_t ind_idx = blk_index - NYOTA_INODE_DIRECT_BLOCKS;
            if (inode->indirect_block != 0) {
                if (!indirect_loaded) {
                    if (nyotafs_read_block(fs, inode->indirect_block, indirect_buf) == 0) {
                        indirect_loaded = true;
                    }
                }
                if (indirect_loaded && ind_idx < (NYOTA_BLOCK_SIZE / sizeof(uint64_t))) {
                    phys_blk = indirect_buf[ind_idx];
                }
            }
        }

        if (phys_blk != 0) {
            if (nyotafs_read_block(fs, phys_blk, block_buf) != 0) {
                return (bytes_read > 0) ? (int)bytes_read : -1;
            }
            memcpy(dest + bytes_read, block_buf + blk_offset, chunk);
        } else {
            /* Sparse hole */
            memset(dest + bytes_read, 0, chunk);
        }

        bytes_read += chunk;
    }

    return (int)bytes_read;
}

int nyotafs_write_file(nyota_fs_t *fs, uint64_t inode_num, nyota_inode_t *inode, uint64_t offset, const void *buf, size_t count) {
    if (!fs || !inode || !buf || count == 0) return -1;

    const uint8_t *src = (const uint8_t *)buf;
    size_t bytes_written = 0;

    uint8_t block_buf[NYOTA_BLOCK_SIZE];
    uint64_t indirect_buf[NYOTA_BLOCK_SIZE / sizeof(uint64_t)];
    bool indirect_dirty = false;
    bool indirect_loaded = false;

    while (bytes_written < count) {
        uint64_t curr_pos = offset + bytes_written;
        uint32_t blk_index = (uint32_t)(curr_pos / NYOTA_BLOCK_SIZE);
        uint32_t blk_offset = (uint32_t)(curr_pos % NYOTA_BLOCK_SIZE);
        size_t chunk = NYOTA_BLOCK_SIZE - blk_offset;
        if (chunk > (count - bytes_written)) {
            chunk = count - bytes_written;
        }

        uint64_t phys_blk = 0;
        if (blk_index < NYOTA_INODE_DIRECT_BLOCKS) {
            if (inode->direct_blocks[blk_index] == 0) {
                if (nyotafs_alloc_block(fs, &phys_blk) != 0) {
                    break;
                }
                inode->direct_blocks[blk_index] = phys_blk;
            } else {
                phys_blk = inode->direct_blocks[blk_index];
            }
        } else {
            uint32_t ind_idx = blk_index - NYOTA_INODE_DIRECT_BLOCKS;
            if (inode->indirect_block == 0) {
                uint64_t ind_blk = 0;
                if (nyotafs_alloc_block(fs, &ind_blk) != 0) {
                    break;
                }
                inode->indirect_block = ind_blk;
                memset(indirect_buf, 0, sizeof(indirect_buf));
                indirect_loaded = true;
                indirect_dirty = true;
            } else if (!indirect_loaded) {
                nyotafs_read_block(fs, inode->indirect_block, indirect_buf);
                indirect_loaded = true;
            }

            if (indirect_buf[ind_idx] == 0) {
                if (nyotafs_alloc_block(fs, &phys_blk) != 0) {
                    break;
                }
                indirect_buf[ind_idx] = phys_blk;
                indirect_dirty = true;
            } else {
                phys_blk = indirect_buf[ind_idx];
            }
        }

        if (blk_offset > 0 || chunk < NYOTA_BLOCK_SIZE) {
            nyotafs_read_block(fs, phys_blk, block_buf);
        }
        memcpy(block_buf + blk_offset, src + bytes_written, chunk);
        nyotafs_write_block(fs, phys_blk, block_buf);

        bytes_written += chunk;
        if (curr_pos + chunk > inode->size) {
            inode->size = curr_pos + chunk;
        }
    }

    if (indirect_dirty && inode->indirect_block != 0) {
        nyotafs_write_block(fs, inode->indirect_block, indirect_buf);
    }

    nyotafs_write_inode(fs, inode_num, inode);
    return (int)bytes_written;
}

/* ── Directory Operations ─────────────────────────────────────────────────── */

int nyotafs_lookup(nyota_fs_t *fs, uint64_t dir_inode_num, const char *name, uint64_t *out_inode) {
    if (!fs || !name || !out_inode) return -1;

    nyota_inode_t dir;
    if (nyotafs_read_inode(fs, dir_inode_num, &dir) != 0) return -1;
    if ((dir.mode & NYOTA_MODE_TYPE_MASK) != NYOTA_MODE_DIR) return -1;

    uint8_t buf[NYOTA_BLOCK_SIZE];
    for (int b = 0; b < NYOTA_INODE_DIRECT_BLOCKS; b++) {
        uint64_t blk = dir.direct_blocks[b];
        if (blk == 0) continue;

        if (nyotafs_read_block(fs, blk, buf) != 0) continue;

        nyota_dirent_t *entries = (nyota_dirent_t *)buf;
        for (size_t i = 0; i < NYOTA_DIRENTS_PER_BLOCK; i++) {
            if (entries[i].inode != 0) {
                if (strcmp(entries[i].name, name) == 0) {
                    *out_inode = entries[i].inode;
                    return 0;
                }
            }
        }
    }

    return -1; /* Not found */
}

int nyotafs_create_entry(nyota_fs_t *fs, uint64_t dir_inode_num, const char *name, uint64_t child_inode, uint8_t type) {
    if (!fs || !name) return -1;

    nyota_inode_t dir;
    if (nyotafs_read_inode(fs, dir_inode_num, &dir) != 0) return -1;
    if ((dir.mode & NYOTA_MODE_TYPE_MASK) != NYOTA_MODE_DIR) return -1;

    uint8_t buf[NYOTA_BLOCK_SIZE];
    for (int b = 0; b < NYOTA_INODE_DIRECT_BLOCKS; b++) {
        if (dir.direct_blocks[b] == 0) {
            uint64_t new_blk;
            if (nyotafs_alloc_block(fs, &new_blk) != 0) return -1;
            dir.direct_blocks[b] = new_blk;
        }

        uint64_t blk = dir.direct_blocks[b];
        if (nyotafs_read_block(fs, blk, buf) != 0) return -1;

        nyota_dirent_t *entries = (nyota_dirent_t *)buf;
        for (size_t i = 0; i < NYOTA_DIRENTS_PER_BLOCK; i++) {
            if (entries[i].inode == 0) {
                entries[i].inode = child_inode;
                entries[i].type = type;
                size_t nlen = strlen(name);
                if (nlen >= NYOTA_NAME_MAX) nlen = NYOTA_NAME_MAX - 1;
                entries[i].name_len = (uint8_t)nlen;
                memcpy(entries[i].name, name, nlen);
                entries[i].name[nlen] = '\0';

                nyotafs_write_block(fs, blk, buf);
                dir.size += sizeof(nyota_dirent_t);
                nyotafs_write_inode(fs, dir_inode_num, &dir);
                return 0;
            }
        }
    }

    return -1; /* Directory full */
}

int nyotafs_delete_entry(nyota_fs_t *fs, uint64_t dir_inode_num, const char *name) {
    if (!fs || !name) return -1;

    nyota_inode_t dir;
    if (nyotafs_read_inode(fs, dir_inode_num, &dir) != 0) return -1;

    uint8_t buf[NYOTA_BLOCK_SIZE];
    for (int b = 0; b < NYOTA_INODE_DIRECT_BLOCKS; b++) {
        uint64_t blk = dir.direct_blocks[b];
        if (blk == 0) continue;

        if (nyotafs_read_block(fs, blk, buf) != 0) continue;

        nyota_dirent_t *entries = (nyota_dirent_t *)buf;
        for (size_t i = 0; i < NYOTA_DIRENTS_PER_BLOCK; i++) {
            if (entries[i].inode != 0 && strcmp(entries[i].name, name) == 0) {
                entries[i].inode = 0;
                entries[i].name[0] = '\0';
                nyotafs_write_block(fs, blk, buf);
                if (dir.size >= sizeof(nyota_dirent_t)) {
                    dir.size -= sizeof(nyota_dirent_t);
                }
                nyotafs_write_inode(fs, dir_inode_num, &dir);
                return 0;
            }
        }
    }

    return -1;
}

/* ── Path Resolution ──────────────────────────────────────────────────────── */

int nyotafs_resolve_path(nyota_fs_t *fs, const char *path, uint64_t *out_inode) {
    if (!fs || !path || !out_inode) return -1;

    if (path[0] == '\0') return -1;

    /* Start at root inode */
    uint64_t curr_inode = fs->sb.root_inode;

    /* If path is just "/" */
    if (path[0] == '/' && path[1] == '\0') {
        *out_inode = curr_inode;
        return 0;
    }

    const char *p = path;
    if (*p == '/') p++;

    char component[NYOTA_NAME_MAX];
    while (*p != '\0') {
        size_t ci = 0;
        while (*p != '/' && *p != '\0' && ci < NYOTA_NAME_MAX - 1) {
            component[ci++] = *p++;
        }
        component[ci] = '\0';

        if (*p == '/') p++;

        if (ci == 0) continue;

        uint64_t next_inode = 0;
        if (nyotafs_lookup(fs, curr_inode, component, &next_inode) != 0) {
            return -1; /* Component not found */
        }
        curr_inode = next_inode;
    }

    *out_inode = curr_inode;
    return 0;
}

int nyotafs_create_file(nyota_fs_t *fs, const char *path, uint32_t mode, uint64_t *out_inode) {
    if (!fs || !path || !out_inode) return -1;

    /* Separate directory and filename */
    const char *last_slash = NULL;
    for (const char *p = path; *p != '\0'; p++) {
        if (*p == '/') last_slash = p;
    }

    char parent_path[128];
    const char *filename = NULL;

    if (!last_slash) {
        memcpy(parent_path, "/", 2);
        filename = path;
    } else if (last_slash == path) {
        memcpy(parent_path, "/", 2);
        filename = last_slash + 1;
    } else {
        size_t plen = last_slash - path;
        if (plen >= sizeof(parent_path)) plen = sizeof(parent_path) - 1;
        memcpy(parent_path, path, plen);
        parent_path[plen] = '\0';
        filename = last_slash + 1;
    }

    uint64_t parent_inode = 0;
    if (nyotafs_resolve_path(fs, parent_path, &parent_inode) != 0) {
        return -1;
    }

    /* Check if already exists */
    uint64_t existing = 0;
    if (nyotafs_lookup(fs, parent_inode, filename, &existing) == 0) {
        *out_inode = existing;
        return 0;
    }

    uint64_t new_ino = 0;
    if (nyotafs_alloc_inode(fs, &new_ino) != 0) return -1;

    nyota_inode_t node;
    memset(&node, 0, sizeof(node));
    node.mode = NYOTA_MODE_FILE | (mode & 0777);
    node.link_count = 1;
    nyotafs_write_inode(fs, new_ino, &node);

    if (nyotafs_create_entry(fs, parent_inode, filename, new_ino, NYOTA_FT_FILE) != 0) {
        nyotafs_free_inode(fs, new_ino);
        return -1;
    }

    *out_inode = new_ino;
    return 0;
}

int nyotafs_mkdir(nyota_fs_t *fs, const char *path, uint32_t mode, uint64_t *out_inode) {
    if (!fs || !path) return -1;

    const char *last_slash = NULL;
    for (const char *p = path; *p != '\0'; p++) {
        if (*p == '/') last_slash = p;
    }

    char parent_path[128];
    const char *dirname = NULL;

    if (!last_slash) {
        memcpy(parent_path, "/", 2);
        dirname = path;
    } else if (last_slash == path) {
        memcpy(parent_path, "/", 2);
        dirname = last_slash + 1;
    } else {
        size_t plen = last_slash - path;
        if (plen >= sizeof(parent_path)) plen = sizeof(parent_path) - 1;
        memcpy(parent_path, path, plen);
        parent_path[plen] = '\0';
        dirname = last_slash + 1;
    }

    uint64_t parent_inode = 0;
    if (nyotafs_resolve_path(fs, parent_path, &parent_inode) != 0) {
        return -1;
    }

    uint64_t existing = 0;
    if (nyotafs_lookup(fs, parent_inode, dirname, &existing) == 0) {
        return -1; /* Already exists */
    }

    uint64_t new_ino = 0;
    if (nyotafs_alloc_inode(fs, &new_ino) != 0) return -1;

    nyota_inode_t node;
    memset(&node, 0, sizeof(node));
    node.mode = NYOTA_MODE_DIR | (mode & 0777);
    node.link_count = 2; /* . and parent */
    nyotafs_write_inode(fs, new_ino, &node);

    /* Add . and .. */
    nyotafs_create_entry(fs, new_ino, ".", new_ino, NYOTA_FT_DIR);
    nyotafs_create_entry(fs, new_ino, "..", parent_inode, NYOTA_FT_DIR);

    /* Link into parent */
    if (nyotafs_create_entry(fs, parent_inode, dirname, new_ino, NYOTA_FT_DIR) != 0) {
        nyotafs_free_inode(fs, new_ino);
        return -1;
    }

    if (out_inode) *out_inode = new_ino;
    return 0;
}

/* ── Mount Filesystem ─────────────────────────────────────────────────────── */

int nyotafs_mount(block_device_t *bdev, nyota_fs_t *fs) {
    if (!bdev || !fs) return -1;

    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print("[FS]    ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_println("Mounting NyotaFS");

    fs->bdev = bdev;
    fs->mounted = false;

    /* Read Superblock at Block 0 */
    uint8_t sb_buf[NYOTA_BLOCK_SIZE];
    if (nyotafs_read_block(fs, 0, sb_buf) != 0) {
        kerror("nyotafs_mount: failed to read superblock block 0");
        return -1;
    }

    memcpy(&fs->sb, sb_buf, sizeof(nyota_superblock_t));

    /* Validate superblock */
    if (fs->sb.magic != (uint32_t)NYOTA_FS_MAGIC) {
        kerror("nyotafs_mount: invalid magic");
        return -1;
    }

    if (fs->sb.version != NYOTA_FS_VERSION) {
        kerror("nyotafs_mount: unsupported version");
        return -1;
    }

    if (fs->sb.block_size != NYOTA_BLOCK_SIZE) {
        kerror("nyotafs_mount: unsupported block size");
        return -1;
    }

    fs->mounted = true;

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("[ OK ]  ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("NyotaFS valid (total blocks: ");
    vga_print_dec(fs->sb.total_blocks);
    vga_println(")");

    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print("[FS]    ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("Root inode: ");
    vga_print_dec(fs->sb.root_inode);
    vga_println("");

    return 0;
}
