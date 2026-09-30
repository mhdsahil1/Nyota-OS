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
#define SYS_PIPE            26
#define SYS_DUP2            27
#define SYS_KILL            28
#define SYS_SIGNAL          29
#define SYS_GETUID          30
#define SYS_SETUID          31
#define SYS_GETGID          32
#define SYS_SETGID          33
#define SYS_CHMOD           34
#define SYS_CHOWN           35
#define SYS_SHM_GET         36
#define SYS_SHM_AT          37
#define SYS_SHM_DT          38
#define SYS_SHM_CTL         39
#define SYS_GETRANDOM       40
#define SYS_SPAWN2          41
#define SYS_SECINFO         42
#define SYS_GETPROCS        43
#define SYS_TIME            44
#define SYS_CLOCK_GETTIME   45
#define SYS_NANOSLEEP       46
#define SYS_CHDIR           47
#define SYS_GETCWD          48
#define SYS_SYNC            49
#define SYS_REBOOT          50
#define SYS_KLOG            51
#define SYS_SYSINFO         52
#define SYS_TTY_CTRL        53
#define SYS_EXECVE          54
#define SYS_SETPGID         55
#define SYS_GETPGID         56
#define SYS_SETSID          57
#define SYS_GETSID          58
#define SYS_UNLINK          59

/* waitpid Options */
#define WNOHANG             1
#define WUNTRACED           2

/* Reboot Commands */
#define REBOOT_CMD_HALT     0
#define REBOOT_CMD_REBOOT   1
#define REBOOT_CMD_POWEROFF 2

/* TTY Control Commands */
#define TTY_CTRL_GET_PGRP   1
#define TTY_CTRL_SET_PGRP   2

typedef struct {
    uint64_t uptime_sec;
    uint64_t total_ram;
    uint64_t free_ram;
    uint64_t used_ram;
    uint32_t process_count;
    char     kernel_ver[16];
    char     machine[16];
} sysinfo_data_t;

/* Syscall Error Codes */
#define SYS_ERR_NONE        0
#define SYS_ERR_EPERM       (-1)   /* Operation not permitted */
#define SYS_ERR_ENOENT      (-2)   /* No such file or directory */
#define SYS_ERR_ESRCH       (-3)   /* No such process */
#define SYS_ERR_EINTR       (-4)   /* Interrupted system call */
#define SYS_ERR_EIO         (-5)   /* I/O error */
#define SYS_ERR_ENOEXEC     (-8)   /* Exec format error */
#define SYS_ERR_EBADF       (-9)   /* Bad file descriptor */
#define SYS_ERR_ECHILD      (-10)  /* No child processes */
#define SYS_ERR_EAGAIN      (-11)  /* Resource temporarily unavailable */
#define SYS_ERR_ENOMEM      (-12)  /* Out of memory */
#define SYS_ERR_EACCES      (-13)  /* Permission denied */
#define SYS_ERR_EFAULT      (-14)  /* Bad address / pointer */
#define SYS_ERR_EEXIST      (-17)  /* File exists */
#define SYS_ERR_ENODEV      (-19)  /* No such device */
#define SYS_ERR_ENOTDIR     (-20)  /* Not a directory */
#define SYS_ERR_EISDIR      (-21)  /* Is a directory */
#define SYS_ERR_EINVAL      (-22)  /* Invalid argument */
#define SYS_ERR_ENOSPC      (-28)  /* No space left on device */
#define SYS_ERR_EPIPE       (-32)  /* Broken pipe */
#define SYS_ERR_ERANGE      (-34)  /* Numerical result out of range */
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
bool user_validate_string(const char *ustr, size_t max_len);
bool size_add_overflow(size_t a, size_t b, size_t *result);
bool size_mul_overflow(size_t a, size_t b, size_t *result);
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
int64_t sys_read(int fd, void *buf, size_t count);
int64_t sys_seek(int fd, int64_t offset, int whence);
int64_t sys_stat(const char *path, void *st);
int64_t sys_getdents(int fd, void *dirp, size_t count);
int64_t sys_mkdir(const char *path, int mode);
int64_t sys_create(const char *path, int mode);
int64_t sys_exec(const char *path, char *const argv[]);
int64_t sys_spawn(const char *path, char *const argv[]);
int64_t sys_waitpid(int32_t pid, int *status);
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
int64_t sys_pipe(int fds[2]);
int64_t sys_dup2(int oldfd, int newfd);
int64_t sys_kill(int32_t pid, int sig);
int64_t sys_signal(int sig, void *handler);
uint32_t sys_getuid(void);
int64_t sys_setuid(uint32_t uid);
uint32_t sys_getgid(void);
int64_t sys_setgid(uint32_t gid);
int64_t sys_chmod(const char *path, uint32_t mode);
int64_t sys_chown(const char *path, uint32_t uid, uint32_t gid);
int64_t sys_shm_get(uint32_t key, size_t size, int flags);
void *sys_shm_at(int shmid, const void *addr, int flags);
int64_t sys_shm_dt(const void *addr);
int64_t sys_shm_ctl(int shmid, int cmd, void *buf);
int64_t sys_getrandom(void *buf, size_t len, unsigned int flags);
int64_t sys_spawn2(const char *path, char *const argv[], int in_fd, int out_fd);
int64_t sys_secinfo(void *info);

#endif /* NYOTA_SYSCALL_H */
