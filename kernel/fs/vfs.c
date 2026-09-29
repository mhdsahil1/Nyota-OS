/* =============================================================================
 * Nyota OS — Virtual Filesystem (VFS) Implementation (Phase 6)
 * File objects, per-process descriptor management, device routing, and syscall bridge.
 * =========================================================================== */

#include "fs/vfs.h"
#include "fs/nyotafs.h"
#include "net/socket.h"
#include "ipc/pipe.h"
#include "process.h"
#include "heap.h"
#include "memory.h"
#include "vga.h"
#include "serial.h"
#include "keyboard.h"
#include "kernel.h"

static nyota_fs_t root_filesystem;
static bool root_mounted = false;

static file_t global_file_table[MAX_OPEN_FILES];

void vfs_init(void) {
    memset(global_file_table, 0, sizeof(global_file_table));
    root_mounted = false;
}

int vfs_mount_root(block_device_t *bdev) {
    if (!bdev) return NYOTA_ENODEV;

    if (nyotafs_mount(bdev, &root_filesystem) != 0) {
        kerror("vfs_mount_root: failed to mount root filesystem");
        return NYOTA_EIO;
    }

    root_mounted = true;

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("[ OK ]  ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_println("/ mounted");

    return NYOTA_OK;
}

nyota_fs_t *vfs_get_root_fs(void) {
    return root_mounted ? &root_filesystem : NULL;
}

/* ── Kernel File Object Allocator ─────────────────────────────────────────── */

/* Forward declaration */
static file_t *get_process_file(int fd);

file_t *vfs_get_file(int fd) {
    return get_process_file(fd);
}

file_t *vfs_create_socket_file(void *sock_ptr) {
    for (size_t i = 0; i < MAX_OPEN_FILES; i++) {
        if (global_file_table[i].ref_count == 0) {
            memset(&global_file_table[i], 0, sizeof(file_t));
            global_file_table[i].type = FILE_TYPE_SOCKET;
            global_file_table[i].flags = O_RDWR;
            global_file_table[i].filesystem_data = sock_ptr;
            global_file_table[i].ref_count = 1;
            return &global_file_table[i];
        }
    }
    return NULL;
}

file_t *vfs_alloc_file(void) {
    for (size_t i = 0; i < MAX_OPEN_FILES; i++) {
        if (global_file_table[i].ref_count == 0) {
            memset(&global_file_table[i], 0, sizeof(file_t));
            global_file_table[i].ref_count = 1;
            return &global_file_table[i];
        }
    }
    return NULL;
}
#define alloc_file_object vfs_alloc_file

static void free_file_object(file_t *f) {
    if (!f) return;
    if (f->ref_count > 0) {
        f->ref_count--;
        if (f->ref_count == 0) {
            if (f->type == FILE_TYPE_SOCKET && f->filesystem_data) {
                socket_close((socket_t *)f->filesystem_data);
            } else if (f->type == FILE_TYPE_PIPE && f->filesystem_data) {
                pipe_close_file(f);
            }
            memset(f, 0, sizeof(file_t));
        }
    }
}

/* ── Process File Descriptor Table APIs ──────────────────────────────────── */

int vfs_init_process_fds(file_t **fds) {
    if (!fds) return NYOTA_EINVAL;

    for (int i = 0; i < MAX_PROCESS_FDS; i++) {
        fds[i] = NULL;
    }

    /* Stdin (FD 0) */
    file_t *in = alloc_file_object();
    if (in) {
        in->type = FILE_TYPE_DEV_CONSOLE;
        in->flags = O_RDONLY;
        fds[STDIN_FILENO] = in;
    }

    /* Stdout (FD 1) */
    file_t *out = alloc_file_object();
    if (out) {
        out->type = FILE_TYPE_DEV_CONSOLE;
        out->flags = O_WRONLY;
        fds[STDOUT_FILENO] = out;
    }

    /* Stderr (FD 2) */
    file_t *err = alloc_file_object();
    if (err) {
        err->type = FILE_TYPE_DEV_CONSOLE;
        err->flags = O_WRONLY;
        fds[STDERR_FILENO] = err;
    }

    return NYOTA_OK;
}

int vfs_alloc_fd(file_t **fds, file_t *f) {
    if (!fds || !f) return NYOTA_EINVAL;

    for (int i = 0; i < MAX_PROCESS_FDS; i++) {
        if (fds[i] == NULL) {
            fds[i] = f;
            return i;
        }
    }
    return NYOTA_ENOSPC;
}

int vfs_close_process_fds(file_t **fds) {
    if (!fds) return NYOTA_EINVAL;

    for (int i = 0; i < MAX_PROCESS_FDS; i++) {
        if (fds[i]) {
            free_file_object(fds[i]);
            fds[i] = NULL;
        }
    }
    return NYOTA_OK;
}

int vfs_close_file(file_t *f) {
    if (!f) return NYOTA_EINVAL;
    free_file_object(f);
    return NYOTA_OK;
}

static file_t *get_process_file(int fd) {
    if (fd < 0 || fd >= MAX_PROCESS_FDS) return NULL;
    process_t *curr = process_get_current();
    if (!curr) return NULL;
    return curr->fds[fd];
}

/* ── File Operations ──────────────────────────────────────────────────────── */

int vfs_open(const char *path, int flags, int mode) {
    (void)mode;
    if (!path) return NYOTA_EFAULT;

    process_t *curr = process_get_current();
    if (!curr) return NYOTA_EBADF;

    /* Check for device files */
    if (strcmp(path, "/dev/console") == 0 || strcmp(path, "dev/console") == 0) {
        file_t *f = alloc_file_object();
        if (!f) return NYOTA_ENOMEM;
        f->type = FILE_TYPE_DEV_CONSOLE;
        f->flags = flags;
        int fd = vfs_alloc_fd(curr->fds, f);
        if (fd < 0) free_file_object(f);
        return fd;
    }

    if (strcmp(path, "/dev/null") == 0 || strcmp(path, "dev/null") == 0) {
        file_t *f = alloc_file_object();
        if (!f) return NYOTA_ENOMEM;
        f->type = FILE_TYPE_DEV_NULL;
        f->flags = flags;
        int fd = vfs_alloc_fd(curr->fds, f);
        if (fd < 0) free_file_object(f);
        return fd;
    }

    if (!root_mounted) return NYOTA_ENODEV;

    uint64_t inode_num = 0;
    int res = nyotafs_resolve_path(&root_filesystem, path, &inode_num);

    if (res != 0) {
        if (flags & O_CREAT) {
            res = nyotafs_create_file(&root_filesystem, path, 0755, &inode_num);
            if (res != 0) return NYOTA_ENOENT;
        } else {
            return NYOTA_ENOENT;
        }
    }

    nyota_inode_t inode;
    if (nyotafs_read_inode(&root_filesystem, inode_num, &inode) != 0) {
        return NYOTA_EIO;
    }

    /* Permission checks */
    if (curr->uid != 0) {
        bool need_read = ((flags & O_ACCMODE) == O_RDONLY) || ((flags & O_ACCMODE) == O_RDWR);
        bool need_write = ((flags & O_ACCMODE) == O_WRONLY) || ((flags & O_ACCMODE) == O_RDWR) || (flags & O_TRUNC);

        /* Protect system binaries and directories from modification */
        if (need_write) {
            if (strncmp(path, "/bin/", 5) == 0 || strncmp(path, "bin/", 4) == 0 ||
                strncmp(path, "/etc/", 5) == 0 || strncmp(path, "etc/", 4) == 0 ||
                strncmp(path, "/kernel", 7) == 0 || strncmp(path, "kernel", 6) == 0) {
                return NYOTA_EACCES;
            }
        }

        uint32_t perms = inode.mode & 0777;
        bool allowed = false;
        if (curr->uid == inode.uid) {
            bool ok = true;
            if (need_read && !(perms & 0400)) ok = false;
            if (need_write && !(perms & 0200)) ok = false;
            if (ok) allowed = true;
        } else if (curr->gid == inode.gid) {
            bool ok = true;
            if (need_read && !(perms & 0040)) ok = false;
            if (need_write && !(perms & 0020)) ok = false;
            if (ok) allowed = true;
        } else {
            bool ok = true;
            if (need_read && !(perms & 0004)) ok = false;
            if (need_write && !(perms & 0002)) ok = false;
            if (ok) allowed = true;
        }

        if (!allowed) {
            return NYOTA_EACCES;
        }
    }

    file_t *f = alloc_file_object();
    if (!f) return NYOTA_ENOMEM;

    f->flags = flags;
    f->inode = inode_num;
    f->filesystem_data = &root_filesystem;

    if ((inode.mode & NYOTA_MODE_TYPE_MASK) == NYOTA_MODE_DIR) {
        f->type = FILE_TYPE_DIR;
    } else {
        f->type = FILE_TYPE_REGULAR;
    }

    if (flags & O_APPEND) {
        f->offset = inode.size;
    } else {
        f->offset = 0;
    }

    int fd = vfs_alloc_fd(curr->fds, f);
    if (fd < 0) {
        free_file_object(f);
        return NYOTA_ENOSPC;
    }

    return fd;
}

int vfs_close(int fd) {
    if (fd < 0 || fd >= MAX_PROCESS_FDS) return NYOTA_EBADF;

    process_t *curr = process_get_current();
    if (!curr) return NYOTA_EBADF;

    file_t *f = curr->fds[fd];
    if (!f) return NYOTA_EBADF;

    curr->fds[fd] = NULL;
    free_file_object(f);
    return NYOTA_OK;
}

int vfs_dup2(int oldfd, int newfd) {
    process_t *curr = process_get_current();
    if (!curr) return NYOTA_EBADF;
    if (oldfd < 0 || oldfd >= MAX_PROCESS_FDS) return NYOTA_EBADF;
    if (newfd < 0 || newfd >= MAX_PROCESS_FDS) return NYOTA_EBADF;
    if (!curr->fds[oldfd]) return NYOTA_EBADF;
    if (oldfd == newfd) return newfd;

    if (curr->fds[newfd]) {
        free_file_object(curr->fds[newfd]);
        curr->fds[newfd] = NULL;
    }

    curr->fds[newfd] = curr->fds[oldfd];
    curr->fds[oldfd]->ref_count++;
    return newfd;
}

int64_t vfs_read(int fd, void *buf, size_t count) {
    if (count == 0) return 0;
    if (!buf) return NYOTA_EFAULT;

    file_t *f = get_process_file(fd);
    if (!f) return NYOTA_EBADF;

    if (f->type == FILE_TYPE_DEV_NULL) {
        return 0; /* EOF */
    }

    if (f->type == FILE_TYPE_DEV_CONSOLE) {
        /* Read interactively from keyboard */
        char *dst = (char *)buf;
        size_t n = 0;
        while (n < count) {
            char c = keyboard_getchar();
            if (c == '\r' || c == '\n') {
                vga_putchar('\n');
                dst[n++] = '\n';
                break;
            } else if (c == '\b') {
                if (n > 0) {
                    n--;
                    vga_putchar('\b');
                }
            } else if (c >= 32 && c <= 126) {
                dst[n++] = c;
                vga_putchar(c);
            }
        }
        return (int64_t)n;
    }

    if (f->type == FILE_TYPE_SOCKET) {
        return socket_recv((socket_t *)f->filesystem_data, buf, count, 0);
    }

    if (f->type == FILE_TYPE_PIPE) {
        return pipe_read((pipe_t *)f->filesystem_data, buf, count);
    }

    if (f->type == FILE_TYPE_REGULAR) {
        nyota_fs_t *fs = (nyota_fs_t *)f->filesystem_data;
        if (!fs || (uint64_t)fs < 0x100000 || (uint64_t)fs >= USER_SPACE_BASE) {
            serial_write("[VFS_READ ERROR] Invalid fs! fd="); serial_write_dec(fd);
            serial_write(" f="); serial_write_hex((uint64_t)f);
            serial_write(" type="); serial_write_dec((uint64_t)f->type);
            serial_write(" fs="); serial_write_hex((uint64_t)fs); serial_write("\n");
            return NYOTA_EIO;
        }

        nyota_inode_t inode;
        if (nyotafs_read_inode(fs, f->inode, &inode) != 0) {
            return NYOTA_EIO;
        }

        int bytes = nyotafs_read_file(fs, &inode, f->offset, buf, count);
        if (bytes > 0) {
            f->offset += bytes;
        }
        return bytes;
    }

    return NYOTA_EINVAL;
}

int64_t vfs_write(int fd, const void *buf, size_t count) {
    if (count == 0) return 0;
    if (!buf) return NYOTA_EFAULT;

    file_t *f = get_process_file(fd);
    if (!f) return NYOTA_EBADF;

    if (f->type == FILE_TYPE_DEV_NULL) {
        return (int64_t)count; /* Discarded */
    }

    if (f->type == FILE_TYPE_DEV_CONSOLE) {
        const char *src = (const char *)buf;
        for (size_t i = 0; i < count; i++) {
            vga_putchar(src[i]);
        }
        return (int64_t)count;
    }

    if (f->type == FILE_TYPE_SOCKET) {
        return socket_send((socket_t *)f->filesystem_data, buf, count, 0);
    }

    if (f->type == FILE_TYPE_PIPE) {
        return pipe_write((pipe_t *)f->filesystem_data, buf, count);
    }

    if (f->type == FILE_TYPE_REGULAR) {
        nyota_fs_t *fs = (nyota_fs_t *)f->filesystem_data;
        if (!fs || (uint64_t)fs < 0x100000 || (uint64_t)fs >= USER_SPACE_BASE) {
            serial_write("[VFS_WRITE ERROR] Invalid fs! fd="); serial_write_dec(fd);
            serial_write(" f="); serial_write_hex((uint64_t)f);
            serial_write(" type="); serial_write_dec((uint64_t)f->type);
            serial_write(" fs="); serial_write_hex((uint64_t)fs); serial_write("\n");
            return NYOTA_EIO;
        }

        nyota_inode_t inode;
        if (nyotafs_read_inode(fs, f->inode, &inode) != 0) {
            return NYOTA_EIO;
        }


        int bytes = nyotafs_write_file(fs, f->inode, &inode, f->offset, buf, count);
        if (bytes > 0) {
            f->offset += bytes;
        }
        return bytes;
    }

    return NYOTA_EINVAL;
}

int64_t vfs_seek(int fd, int64_t offset, int whence) {
    file_t *f = get_process_file(fd);
    if (!f) return NYOTA_EBADF;

    if (f->type != FILE_TYPE_REGULAR) {
        return NYOTA_EINVAL;
    }

    nyota_fs_t *fs = (nyota_fs_t *)f->filesystem_data;
    if (!fs || (uint64_t)fs < 0x100000 || (uint64_t)fs >= USER_SPACE_BASE) return NYOTA_EIO;
    nyota_inode_t inode;
    if (nyotafs_read_inode(fs, f->inode, &inode) != 0) {
        return NYOTA_EIO;
    }

    int64_t new_offset = 0;
    switch (whence) {
        case SEEK_SET:
            new_offset = offset;
            break;
        case SEEK_CUR:
            new_offset = (int64_t)f->offset + offset;
            break;
        case SEEK_END:
            new_offset = (int64_t)inode.size + offset;
            break;
        default:
            return NYOTA_EINVAL;
    }

    if (new_offset < 0) return NYOTA_EINVAL;
    f->offset = (uint64_t)new_offset;
    return new_offset;
}

int vfs_stat(const char *path, vfs_stat_t *st) {
    if (!path || !st) return NYOTA_EFAULT;
    if (!root_mounted) return NYOTA_ENODEV;

    uint64_t inode_num = 0;
    if (nyotafs_resolve_path(&root_filesystem, path, &inode_num) != 0) {
        return NYOTA_ENOENT;
    }

    nyota_inode_t inode;
    if (nyotafs_read_inode(&root_filesystem, inode_num, &inode) != 0) {
        return NYOTA_EIO;
    }

    st->inode = inode_num;
    st->mode = inode.mode;
    st->size = inode.size;
    st->uid = inode.uid;
    st->gid = inode.gid;
    st->created = inode.created;
    st->modified = inode.modified;

    return NYOTA_OK;
}

int vfs_fstat(int fd, vfs_stat_t *st) {
    if (!st) return NYOTA_EFAULT;
    file_t *f = get_process_file(fd);
    if (!f) return NYOTA_EBADF;

    if (f->type == FILE_TYPE_DEV_CONSOLE || f->type == FILE_TYPE_DEV_NULL || f->type == FILE_TYPE_SOCKET || f->type == FILE_TYPE_PIPE) {
        st->inode = 0;
        st->mode = (f->type == FILE_TYPE_SOCKET ? 0140666 : (f->type == FILE_TYPE_PIPE ? 0010660 : (NYOTA_MODE_DEV | 0666)));
        st->size = 0;
        st->uid = 0;
        st->gid = 0;
        st->created = 0;
        st->modified = 0;
        return NYOTA_OK;
    }

    nyota_fs_t *fs = (nyota_fs_t *)f->filesystem_data;
    if (!fs || (uint64_t)fs < 0x100000 || (uint64_t)fs >= USER_SPACE_BASE) return NYOTA_EIO;

    nyota_inode_t inode;
    if (nyotafs_read_inode(fs, f->inode, &inode) != 0) {
        return NYOTA_EIO;
    }

    st->inode = f->inode;
    st->mode = inode.mode;
    st->size = inode.size;
    st->uid = inode.uid;
    st->gid = inode.gid;
    st->created = inode.created;
    st->modified = inode.modified;

    return NYOTA_OK;
}

int vfs_readdir(int fd, vfs_dirent_t *dirp) {
    if (!dirp) return NYOTA_EFAULT;
    file_t *f = get_process_file(fd);
    if (!f || f->type != FILE_TYPE_DIR) return NYOTA_EBADF;

    nyota_fs_t *fs = (nyota_fs_t *)f->filesystem_data;
    if (!fs || (uint64_t)fs < 0x100000 || (uint64_t)fs >= USER_SPACE_BASE) return NYOTA_EIO;

    nyota_inode_t dir;
    if (nyotafs_read_inode(fs, f->inode, &dir) != 0) return NYOTA_EIO;

    uint8_t buf[NYOTA_BLOCK_SIZE];
    uint64_t entry_idx = f->offset;

    for (int b = 0; b < NYOTA_INODE_DIRECT_BLOCKS; b++) {
        uint64_t blk = dir.direct_blocks[b];
        if (blk == 0) continue;

        if (nyotafs_read_block(fs, blk, buf) != 0) continue;

        nyota_dirent_t *entries = (nyota_dirent_t *)buf;
        for (size_t i = 0; i < NYOTA_DIRENTS_PER_BLOCK; i++) {
            if (entries[i].inode != 0) {
                if (entry_idx == 0) {
                    dirp->inode = entries[i].inode;
                    dirp->type = entries[i].type;
                    size_t nlen = strlen(entries[i].name);
                    if (nlen >= NYOTA_NAME_MAX) nlen = NYOTA_NAME_MAX - 1;
                    memcpy(dirp->name, entries[i].name, nlen);
                    dirp->name[nlen] = '\0';

                    f->offset++;
                    return 1; /* Found one */
                }
                entry_idx--;
            }
        }
    }

    return 0; /* EOF */
}

int vfs_mkdir(const char *path, int mode) {
    if (!path) return NYOTA_EFAULT;
    if (!root_mounted) return NYOTA_ENODEV;
    return nyotafs_mkdir(&root_filesystem, path, (uint32_t)mode, NULL);
}

int vfs_chmod(const char *path, uint32_t mode) {
    if (!path) return NYOTA_EFAULT;
    if (!root_mounted) return NYOTA_ENODEV;
    process_t *curr = process_get_current();
    if (!curr) return NYOTA_EBADF;

    uint64_t inode_num = 0;
    if (nyotafs_resolve_path(&root_filesystem, path, &inode_num) != 0) {
        return NYOTA_ENOENT;
    }

    nyota_inode_t inode;
    if (nyotafs_read_inode(&root_filesystem, inode_num, &inode) != 0) {
        return NYOTA_EIO;
    }

    /* Only owner or root (UID 0) can chmod */
    if (curr->uid != 0 && curr->uid != inode.uid) {
        return NYOTA_EPERM;
    }

    inode.mode = (inode.mode & NYOTA_MODE_TYPE_MASK) | (mode & 07777);
    if (nyotafs_write_inode(&root_filesystem, inode_num, &inode) != 0) {
        return NYOTA_EIO;
    }
    return NYOTA_OK;
}

int vfs_chown(const char *path, uint32_t uid, uint32_t gid) {
    if (!path) return NYOTA_EFAULT;
    if (!root_mounted) return NYOTA_ENODEV;
    process_t *curr = process_get_current();
    if (!curr) return NYOTA_EBADF;

    /* Only root (UID 0) can chown arbitrarily */
    if (curr->uid != 0) {
        return NYOTA_EPERM;
    }

    uint64_t inode_num = 0;
    if (nyotafs_resolve_path(&root_filesystem, path, &inode_num) != 0) {
        return NYOTA_ENOENT;
    }

    nyota_inode_t inode;
    if (nyotafs_read_inode(&root_filesystem, inode_num, &inode) != 0) {
        return NYOTA_EIO;
    }

    inode.uid = uid;
    inode.gid = gid;
    if (nyotafs_write_inode(&root_filesystem, inode_num, &inode) != 0) {
        return NYOTA_EIO;
    }
    return NYOTA_OK;
}
