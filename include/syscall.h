/* =============================================================================
 * Nyota OS — System Call Interface & ABI (Phase 6)
 * Vector 0x80 syscalls with System V register ABI, file I/O, process execution.
 * =========================================================================== */

#ifndef NYOTA_SYSCALL_H
#define NYOTA_SYSCALL_H

#include "types.h"
#include "interrupts.h"

/* Syscall Numbers */
#define SYS_WRITE           0
#define SYS_EXIT            1
#define SYS_GETPID          2
#define SYS_YIELD           3
#define SYS_SLEEP           4
#define SYS_OPEN            5
#define SYS_CLOSE           6
#define SYS_READ            7
#define SYS_SEEK            8
#define SYS_STAT            9
#define SYS_GETDENTS        10
#define SYS_MKDIR           11
#define SYS_CREATE          12
#define SYS_EXEC            13
#define SYS_SPAWN           14
#define SYS_WAITPID         15
#define SYS_SOCKET          16
#define SYS_BIND            17
#define SYS_LISTEN          18
#define SYS_ACCEPT          19
#define SYS_CONNECT         20
#define SYS_SEND            21
#define SYS_RECV            22
#define SYS_SENDTO          23
#define SYS_RECVFROM        24
#define SYS_SHUTDOWN        25

/* Syscall Error Codes */
#define SYS_ERR_NONE        0
#define SYS_ERR_EPERM       (-1)   /* Operation not permitted */
#define SYS_ERR_ENOENT      (-2)   /* No such file or directory */
#define SYS_ERR_EIO         (-5)   /* I/O error */
#define SYS_ERR_ENOEXEC     (-8)   /* Exec format error */
#define SYS_ERR_EBADF       (-9)   /* Bad file descriptor */
#define SYS_ERR_ENOMEM      (-12)  /* Out of memory */
#define SYS_ERR_EACCES      (-13)  /* Permission denied */
#define SYS_ERR_EFAULT      (-14)  /* Bad address / pointer */
#define SYS_ERR_EEXIST      (-17)  /* File exists */
#define SYS_ERR_ENODEV      (-19)  /* No such device */
#define SYS_ERR_ENOTDIR     (-20)  /* Not a directory */
#define SYS_ERR_EISDIR      (-21)  /* Is a directory */
#define SYS_ERR_EINVAL      (-22)  /* Invalid argument */
#define SYS_ERR_ENOSPC      (-28)  /* No space left on device */
#define SYS_ERR_ENOSYS      (-38)  /* Function not implemented */
#define SYS_ERR_ENETDOWN    (-100)
#define SYS_ERR_ENETUNREACH (-101)
#define SYS_ERR_ECONNRESET  (-104)
#define SYS_ERR_ENOBUFS     (-105)
#define SYS_ERR_ETIMEDOUT   (-110)
#define SYS_ERR_ECONNREFUSED (-111)
#define SYS_ERR_EHOSTUNREACH (-113)
#define SYS_ERR_EADDRINUSE  (-115)
#define SYS_ERR_EADDRNOTAVAIL (-116)

/* Syscall Dispatcher APIs */
void syscall_init(void);
void syscall_handler(interrupt_frame_t *frame);
int64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6);

/* User Memory Validation & Safe Copy */
bool user_validate_pointer(const void *uptr, size_t len, bool write);
int copy_from_user(void *kdest, const void *usrc, size_t max_len);
int copy_to_user(void *udest, const void *ksrc, size_t len);
int copy_string_from_user(char *kdest, const char *usrc, size_t max_len);

/* User-mode Library Wrappers */
int64_t sys_write(int fd, const void *buf, size_t len);
void sys_exit(int status) __attribute__((noreturn));
int32_t sys_getpid(void);
void sys_yield(void);
int64_t sys_sleep(uint64_t ms);
int64_t sys_open(const char *path, int flags, int mode);
int64_t sys_close(int fd);
int64_t sys_read(int fd, void *buf, size_t len);
int64_t sys_seek(int fd, int64_t offset, int whence);
int64_t sys_stat(const char *path, void *st);
int64_t sys_getdents(int fd, void *dirp, size_t count);
int64_t sys_mkdir(const char *path, int mode);
int64_t sys_create(const char *path, int mode);
int64_t sys_exec(const char *path, char *const argv[]);
int64_t sys_spawn(const char *path, char *const argv[]);
int64_t sys_waitpid(uint32_t pid, int *status);
int64_t sys_socket(int domain, int type, int protocol);
int64_t sys_bind(int fd, const void *addr, size_t addrlen);
int64_t sys_listen(int fd, int backlog);
int64_t sys_accept(int fd, void *addr, void *addrlen);
int64_t sys_connect(int fd, const void *addr, size_t addrlen);
int64_t sys_send(int fd, const void *buf, size_t len, int flags);
int64_t sys_recv(int fd, void *buf, size_t len, int flags);
int64_t sys_sendto(int fd, const void *buf, size_t len, int flags, const void *dest, size_t addrlen);
int64_t sys_recvfrom(int fd, void *buf, size_t len, int flags, void *src, void *addrlen);
int64_t sys_shutdown(int fd, int how);

#endif /* NYOTA_SYSCALL_H */
