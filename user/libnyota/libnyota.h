/* =============================================================================
 * Nyota OS — Userspace Standard C Library Header (libnyota)
 * System call wrappers, standard I/O, string manipulation, process management.
 * Pure freestanding definitions without host CRT dependencies.
 * =========================================================================== */

#ifndef LIBNYOTA_H
#define LIBNYOTA_H

/* Pure freestanding type definitions based on GCC/Clang builtins */
typedef __INT8_TYPE__      int8_t;
typedef __INT16_TYPE__     int16_t;
typedef __INT32_TYPE__     int32_t;
typedef __INT64_TYPE__     int64_t;

typedef __UINT8_TYPE__     uint8_t;
typedef __UINT16_TYPE__    uint16_t;
typedef __UINT32_TYPE__    uint32_t;
typedef __UINT64_TYPE__    uint64_t;

typedef __SIZE_TYPE__      size_t;
typedef __INTPTR_TYPE__    intptr_t;
typedef __UINTPTR_TYPE__   uintptr_t;

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 202311L)
/* C23 has bool, true, false as built-in keywords */
#else
#ifndef bool
typedef _Bool              bool;
#define true               1
#define false              0
#endif
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

#define STDIN_FILENO    0
#define STDOUT_FILENO   1
#define STDERR_FILENO   2

#define O_RDONLY        0x0000
#define O_WRONLY        0x0001
#define O_RDWR          0x0002
#define O_CREAT         0x0040
#define O_TRUNC         0x0200
#define O_APPEND        0x0400

#define SEEK_SET        0
#define SEEK_CUR        1
#define SEEK_END        2

#define NYOTA_NAME_MAX  54

typedef struct {
    uint64_t inode;
    uint32_t mode;
    uint64_t size;
    uint32_t uid;
    uint32_t gid;
    uint64_t created;
    uint64_t modified;
} stat_t;

typedef struct {
    uint64_t inode;
    uint8_t  type;
    char     name[NYOTA_NAME_MAX];
} dirent_t;

/* System Calls */
int64_t write(int fd, const void *buf, size_t count);
int64_t read(int fd, void *buf, size_t count);
int open(const char *path, int flags, ...);
int close(int fd);
int64_t seek(int fd, int64_t offset, int whence);
int stat(const char *path, stat_t *st);
int getdents(int fd, dirent_t *dirp);
int mkdir(const char *path, int mode);
void exit(int status) __attribute__((noreturn));
int getpid(void);
void yield(void);
int sleep(uint64_t ms);
int exec(const char *path, char *const argv[]);
int spawn(const char *path, char *const argv[]);
int spawn2(const char *path, char *const argv[], int in_fd, int out_fd);
int waitpid(int pid, int *status);

/* Signals */
#define SIGHUP          1
#define SIGINT          2
#define SIGQUIT         3
#define SIGILL          4
#define SIGTRAP         5
#define SIGABRT         6
#define SIGFPE          8
#define SIGKILL         9
#define SIGSEGV         11
#define SIGPIPE         13
#define SIGALRM         14
#define SIGTERM         15
#define SIGCHLD         17
#define SIGCONT         18
#define SIGSTOP         19

#define SIG_DFL         ((void (*)(int))0)
#define SIG_IGN         ((void (*)(int))1)

/* IPC & Security System Calls */
int pipe(int fds[2]);
int dup2(int oldfd, int newfd);
int kill(int pid, int sig);
int signal(int sig, void (*handler)(int));
uint32_t getuid(void);
int setuid(uint32_t uid);
uint32_t getgid(void);
int setgid(uint32_t gid);
int chmod(const char *path, uint32_t mode);
int chown(const char *path, uint32_t uid, uint32_t gid);

#define IPC_CREAT       01000
#define IPC_EXCL        02000
#define IPC_RMID        0
#define IPC_STAT        1

int shm_get(uint32_t key, size_t size, int flags);
void *shm_at(int shmid, const void *addr, int flags);
int shm_dt(const void *addr);
int shm_ctl(int shmid, int cmd, void *buf);

int64_t getrandom(void *buf, size_t len, unsigned int flags);

typedef struct secinfo {
    bool isolation_enabled;
    bool guard_pages_enabled;
    bool pointer_validation_enabled;
    bool capabilities_enabled;
    bool resource_limits_enabled;
    bool aslr_enabled;
    uint32_t active_processes;
    uint32_t active_pipes;
    uint32_t active_shm_segments;
} secinfo_t;

int secinfo(secinfo_t *info);

typedef struct {
    uint32_t pid;
    uint32_t ppid;
    uint32_t uid;
    char state[16];
    char name[32];
} proc_info_t;

int getprocs(proc_info_t *buf, size_t max_count);

/* BSD Socket Constants */
#define AF_UNSPEC       0
#define AF_INET         2

#define SOCK_STREAM     1
#define SOCK_DGRAM      2
#define SOCK_RAW        3

#define IPPROTO_IP      0
#define IPPROTO_ICMP    1
#define IPPROTO_TCP     6
#define IPPROTO_UDP     17

#define SHUT_RD         0
#define SHUT_WR         1
#define SHUT_RDWR       2

/* BSD Socket Address Structures */
struct in_addr {
    uint32_t s_addr;
};

struct sockaddr {
    uint16_t sa_family;
    char     sa_data[14];
};

struct sockaddr_in {
    uint16_t       sin_family;
    uint16_t       sin_port;
    struct in_addr sin_addr;
    char           sin_zero[8];
};

/* Byte Order Conversion Helpers */
static inline uint16_t htons(uint16_t val) {
    return (uint16_t)(((val & 0xFF) << 8) | ((val >> 8) & 0xFF));
}

static inline uint16_t ntohs(uint16_t val) {
    return htons(val);
}

static inline uint32_t htonl(uint32_t val) {
    return (((val & 0x000000FFU) << 24) |
            ((val & 0x0000FF00U) << 8)  |
            ((val & 0x00FF0000U) >> 8)  |
            ((val & 0xFF000000U) >> 24));
}

static inline uint32_t ntohl(uint32_t val) {
    return htonl(val);
}

/* IPv4 Address Parsing & Formatting */
uint32_t inet_addr(const char *cp);
char    *inet_ntoa(struct in_addr in);

/* Socket System Calls */
int     socket(int domain, int type, int protocol);
int     bind(int fd, const struct sockaddr *addr, size_t addrlen);
int     listen(int fd, int backlog);
int     accept(int fd, struct sockaddr *addr, size_t *addrlen);
int     connect(int fd, const struct sockaddr *addr, size_t addrlen);
int64_t send(int fd, const void *buf, size_t len, int flags);
int64_t recv(int fd, void *buf, size_t len, int flags);
int64_t sendto(int fd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, size_t addrlen);
int64_t recvfrom(int fd, void *buf, size_t len, int flags, struct sockaddr *src_addr, size_t *addrlen);
int     shutdown(int fd, int how);

/* String & Memory Functions */
size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);

/* Standard I/O Functions */
int putchar(int c);
int puts(const char *s);
int getchar(void);
int getline(char *buf, size_t size);
int printf(const char *format, ...);

#endif /* LIBNYOTA_H */
