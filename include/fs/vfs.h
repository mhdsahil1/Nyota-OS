/* =============================================================================
 * Nyota OS — Virtual Filesystem (VFS) Interface (Phase 6)
 * Unified filesystem abstraction, file objects, and descriptor tables.
 * =========================================================================== */

#ifndef NYOTA_FS_VFS_H
#define NYOTA_FS_VFS_H

#include "types.h"
#include "fs/nyotafs.h"

#define MAX_OPEN_FILES      64
#define MAX_PROCESS_FDS     16

#define STDIN_FILENO        0
#define STDOUT_FILENO       1
#define STDERR_FILENO       2

/* File Open Flags */
#define O_RDONLY            0x0000
#define O_WRONLY            0x0001
#define O_RDWR              0x0002
#define O_ACCMODE           0x0003
#define O_CREAT             0x0040
#define O_EXCL              0x0080
#define O_TRUNC             0x0200
#define O_APPEND            0x0400

/* Seek Whence */
#define SEEK_SET            0
#define SEEK_CUR            1
#define SEEK_END            2

/* VFS Error Codes */
#define NYOTA_OK            0
#define NYOTA_EPERM        -1
#define NYOTA_ENOENT       -2
#define NYOTA_EIO          -5
#define NYOTA_ENOEXEC      -8
#define NYOTA_EBADF        -9
#define NYOTA_ENOMEM       -12
#define NYOTA_EACCES       -13
#define NYOTA_EFAULT       -14
#define NYOTA_EEXIST       -17
#define NYOTA_ENODEV       -19
#define NYOTA_ENOTDIR      -20
#define NYOTA_EISDIR       -21
#define NYOTA_EINVAL       -22
#define NYOTA_ENOSPC       -28
#define NYOTA_ENOSYS       -38

typedef enum {
    FILE_TYPE_NONE = 0,
    FILE_TYPE_REGULAR,
    FILE_TYPE_DIR,
    FILE_TYPE_DEV_CONSOLE,
    FILE_TYPE_DEV_NULL,
    FILE_TYPE_SOCKET
} file_type_t;

/* File Object (Kernel open file description) */
typedef struct file {
    file_type_t type;
    uint32_t    flags;
    uint64_t    offset;
    uint64_t    inode;
    uint32_t    ref_count;
    void       *filesystem_data;
} file_t;

/* Stat Structure */
typedef struct vfs_stat {
    uint64_t inode;
    uint32_t mode;
    uint64_t size;
    uint32_t uid;
    uint32_t gid;
    uint64_t created;
    uint64_t modified;
} vfs_stat_t;

/* Directory Entry for readdir */
typedef struct vfs_dirent {
    uint64_t inode;
    uint8_t  type;
    char     name[NYOTA_NAME_MAX];
} vfs_dirent_t;

/* VFS Subsystem APIs */
void vfs_init(void);
int vfs_mount_root(block_device_t *bdev);
nyota_fs_t *vfs_get_root_fs(void);

/* Process File Descriptor APIs */
int vfs_init_process_fds(file_t **fds);
int vfs_alloc_fd(file_t **fds, file_t *f);
int vfs_close_process_fds(file_t **fds);
file_t *vfs_create_socket_file(void *sock_ptr);
file_t *vfs_get_file(int fd);

/* File Operations */
int vfs_open(const char *path, int flags, int mode);
int vfs_close(int fd);
int64_t vfs_read(int fd, void *buf, size_t count);
int64_t vfs_write(int fd, const void *buf, size_t count);
int64_t vfs_seek(int fd, int64_t offset, int whence);
int vfs_stat(const char *path, vfs_stat_t *st);
int vfs_fstat(int fd, vfs_stat_t *st);
int vfs_readdir(int fd, vfs_dirent_t *dirp);
int vfs_mkdir(const char *path, int mode);

#endif /* NYOTA_FS_VFS_H */
