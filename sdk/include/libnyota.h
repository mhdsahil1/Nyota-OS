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

typedef struct dirent {
    union {
        uint64_t inode;
        uint64_t d_ino;
    };
    union {
        uint8_t  type;
        uint8_t  d_type;
    };
    union {
        char     name[NYOTA_NAME_MAX];
        char     d_name[NYOTA_NAME_MAX];
    };
} dirent_t;

typedef struct DIR {
    int fd;
    dirent_t cur_entry;
} DIR;

DIR      *opendir(const char *name);
dirent_t *readdir(DIR *dirp);
int       closedir(DIR *dirp);
void      rewinddir(DIR *dirp);

/* System Calls */
int64_t write(int fd, const void *buf, size_t count);
int64_t read(int fd, void *buf, size_t count);
int open(const char *path, int flags, ...);
int close(int fd);
int64_t seek(int fd, int64_t offset, int whence);
int stat(const char *path, stat_t *st);
int getdents(int fd, dirent_t *dirp);
#define F_OK            0
#define X_OK            1
#define W_OK            2
#define R_OK            4

int access(const char *path, int mode);
int mkdir(const char *path, int mode);
int unlink(const char *path);
void exit(int status) __attribute__((noreturn));
int getpid(void);
void yield(void);
int sleep(uint64_t ms);
int exec(const char *path, char *const argv[]);
int spawn(const char *path, char *const argv[]);
int spawn2(const char *path, char *const argv[], int in_fd, int out_fd);
#define WNOHANG         1
#define WUNTRACED       2

int waitpid(int pid, int *status);
int waitpid_options(int pid, int *status, int options);

/* SMP & Multi-Core Syscalls (Phase 11) */
typedef struct {
    uint32_t cpu_id;
    uint32_t apic_id;
    uint32_t online;
    uint32_t is_bsp;
    uint64_t ticks;
    uint64_t context_switches;
    uint64_t syscall_count;
    uint64_t ipi_count;
    int32_t  current_pid;
    char     current_name[32];
} smp_cpu_info_t;

typedef struct {
    uint32_t cpu_count;
    uint32_t cpu_online;
    smp_cpu_info_t cpus[32];
} smp_info_t;

int sched_setaffinity(int pid, uint64_t mask);
int sched_getaffinity(int pid, uint64_t *mask);
int smp_info(smp_info_t *info);

/* Storage & Hardware Diagnostics (Phase 12) */
#define PCI_MAX_DEVICES 32

typedef struct {
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  irq_line;
    char     description[48];
    uint64_t bar0;
    uint64_t bar0_size;
} pci_info_entry_t;

typedef struct {
    uint32_t count;
    pci_info_entry_t devices[PCI_MAX_DEVICES];
} pci_info_list_t;

typedef struct {
    char     name[32];
    char     type_str[16];   /* "ATA", "AHCI", "NVME", "PARTITION" */
    char     model[48];
    uint64_t capacity_sectors;
    uint32_t sector_size;
    uint32_t read_only;
    uint32_t is_partition;
    char     parent_name[32];
    uint64_t partition_offset;
} blk_info_entry_t;

typedef struct {
    uint32_t count;
    blk_info_entry_t devices[32];
} blk_info_list_t;

typedef struct {
    uint64_t f_bsize;       /* Filesystem block size */
    uint64_t f_blocks;      /* Total data blocks in filesystem */
    uint64_t f_bfree;       /* Free blocks in filesystem */
    uint64_t f_bavail;      /* Free blocks available to unprivileged user */
    uint64_t f_files;       /* Total inodes */
    uint64_t f_ffree;       /* Free inodes */
    uint64_t f_namemax;     /* Maximum filename length */
} statvfs_t;

int pci_info(pci_info_list_t *info);
int blk_info(blk_info_list_t *list);
int blk_read(const char *dev_name, uint64_t lba, uint32_t count, void *buf);
int blk_write(const char *dev_name, uint64_t lba, uint32_t count, const void *buf);
int blk_flush(const char *dev_name);
int statvfs(const char *path, statvfs_t *buf);

#define _WAITPID_2(p, s) waitpid((p), (s))
#define _WAITPID_3(p, s, o) waitpid_options((p), (s), (o))
#define _WAITPID_GET(_1, _2, _3, NAME, ...) NAME
#define waitpid(...) _WAITPID_GET(__VA_ARGS__, _WAITPID_3, _WAITPID_2)(__VA_ARGS__)

/* Error Numbers */
#define EPERM           1
#define ENOENT          2
#define ESRCH           3
#define EINTR           4
#define EIO             5
#define ENOEXEC         8
#define EBADF           9
#define ECHILD          10
#define EAGAIN          11
#define ENOMEM          12
#define EACCES          13
#define EFAULT          14
#define EEXIST          17
#define ENODEV          19
#define ENOTDIR         20
#define EISDIR          21
#define EINVAL          22
#define ENOSPC          28
#define EPIPE           32

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
#define SIGTSTP         20

#define SIG_DFL         ((void (*)(int))0)
#define SIG_IGN         ((void (*)(int))1)

/* IPC & Security System Calls */
int pipe(int fds[2]);
int dup2(int oldfd, int newfd);
int kill(int pid, int sig);
int signal(int sig, void (*handler)(int));
uint32_t getuid(void);
int setuid(uint32_t uid);
uint32_t geteuid(void);
int seteuid(uint32_t euid);
uint32_t getgid(void);
int setgid(uint32_t gid);
uint32_t getegid(void);
int setegid(uint32_t egid);
int chmod(const char *path, uint32_t mode);
int chown(const char *path, uint32_t uid, uint32_t gid);

/* Syscall Filter Sandbox Constants & API */
#define SYSCALL_FILTER_OFF       0
#define SYSCALL_FILTER_ALLOW     1
#define SYSCALL_FILTER_DENY      2
int set_syscall_filter(uint8_t mode, uint64_t mask0, uint64_t mask1);

/* Security Audit Logging Constants & API */
#define AUDIT_SEV_INFO    0
#define AUDIT_SEV_WARN    1
#define AUDIT_SEV_DENIED  2
#define AUDIT_SEV_ALERT   3

#define AUDIT_CMD_READ    1
#define AUDIT_CMD_LOG     2
#define AUDIT_CMD_CLEAR   3

typedef struct {
    uint64_t timestamp;
    uint32_t pid;
    uint32_t uid;
    uint8_t  severity;
    char     category[16];
    char     message[80];
} audit_event_t;

int64_t audit_log_op(uint32_t cmd, void *buf, size_t count);

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
#define AF_UNIX         1
#define AF_LOCAL        1
#define AF_INET         2
#define AF_INET6        10

#define SOCK_STREAM     1
#define SOCK_DGRAM      2
#define SOCK_RAW        3

#define IPPROTO_IP      0
#define IPPROTO_ICMP    1
#define IPPROTO_TCP     6
#define IPPROTO_UDP     17
#define IPPROTO_ICMPV6  58

#define SHUT_RD         0
#define SHUT_WR         1
#define SHUT_RDWR       2

struct sockaddr_un {
    uint16_t sun_family;
    char     sun_path[108];
};

/* BSD Socket Address Structures */
struct in_addr {
    uint32_t s_addr;
};

struct in6_addr {
    uint8_t s6_addr[16];
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

struct sockaddr_in6 {
    uint16_t        sin6_family;
    uint16_t        sin6_port;
    uint32_t        sin6_flowinfo;
    struct in6_addr sin6_addr;
    uint32_t        sin6_scope_id;
};

#define INET6_ADDRSTRLEN        46

/* Network Device Information Structures */
#define NETDEV_FLAG_UP          0x0001
#define NETDEV_FLAG_BROADCAST   0x0002
#define NETDEV_FLAG_LOOPBACK    0x0004
#define NETDEV_FLAG_RUNNING     0x0008
#define NETDEV_FLAG_MULTICAST   0x0010

typedef struct {
    char        name[16];
    uint8_t     mac[6];
    uint32_t    ip_addr;
    uint32_t    netmask;
    uint32_t    gateway;
    uint32_t    dns;
    uint8_t     ip6_addr[16];
    bool        has_ip6;
    uint32_t    mtu;
    uint32_t    flags;
    bool        link_up;
    uint64_t    rx_packets;
    uint64_t    tx_packets;
    uint64_t    rx_bytes;
    uint64_t    tx_bytes;
    uint64_t    rx_errors;
    uint64_t    tx_errors;
    uint64_t    rx_dropped;
    uint64_t    tx_dropped;
} netdev_info_t;

typedef struct {
    char        name[16];
    uint32_t    ip_addr;
    uint32_t    netmask;
    uint32_t    gateway;
    uint32_t    dns;
    uint8_t     ip6_addr[16];
    bool        has_ip6;
    uint32_t    mtu;
    bool        set_up;
    bool        is_up;
} netdev_conf_t;

typedef struct {
    uint32_t dest;
    uint32_t netmask;
    uint32_t gateway;
    char     ifname[16];
    bool     valid;
} route_info_t;

#define NET_CTRL_GET_COUNT      1
#define NET_CTRL_GET_DEV        2
#define NET_CTRL_SET_DEV        3
#define NET_CTRL_GET_ROUTES     4
#define NET_CTRL_ADD_ROUTE      5
#define NET_CTRL_DEL_ROUTE      6

int64_t sys_net_ctrl(uint32_t cmd, uint64_t arg1, uint64_t arg2);

/* Firewall Definitions */
#define FIREWALL_MAX_RULES      32
#define FIREWALL_DIR_IN         1
#define FIREWALL_DIR_OUT        2
#define FIREWALL_DIR_BOTH       3
#define FIREWALL_ACTION_DROP    0
#define FIREWALL_ACTION_ACCEPT  1
#define FIREWALL_FAMILY_ANY     0
#define FIREWALL_FAMILY_IPV4    1
#define FIREWALL_FAMILY_IPV6    2
#define FIREWALL_PROTO_ANY      0
#define FIREWALL_PROTO_ICMP     1
#define FIREWALL_PROTO_TCP      6
#define FIREWALL_PROTO_UDP      17
#define FIREWALL_PROTO_ICMPV6   58

typedef struct {
    uint32_t id;
    uint8_t  direction;
    uint8_t  action;
    uint8_t  family;
    uint8_t  protocol;
    uint32_t src_ip;
    uint32_t src_mask;
    uint32_t dst_ip;
    uint32_t dst_mask;
    uint8_t  src_ip6[16];
    uint8_t  src_ip6_prefix;
    uint8_t  dst_ip6[16];
    uint8_t  dst_ip6_prefix;
    uint16_t src_port_start;
    uint16_t src_port_end;
    uint16_t dst_port_start;
    uint16_t dst_port_end;
    char     ifname[16];
    uint64_t packets_matched;
    uint64_t bytes_matched;
    bool     enabled;
} firewall_rule_t;

typedef struct {
    bool     enabled;
    uint8_t  default_in_policy;
    uint8_t  default_out_policy;
    uint32_t rule_count;
    uint64_t total_in_packets;
    uint64_t total_out_packets;
    uint64_t total_accepted;
    uint64_t total_dropped;
} firewall_status_t;

#define FW_CMD_GET_STATUS       1
#define FW_CMD_GET_RULES        2
#define FW_CMD_ADD_RULE         3
#define FW_CMD_DEL_RULE         4
#define FW_CMD_SET_POLICY       5
#define FW_CMD_SET_ENABLED      6
#define FW_CMD_FLUSH            7

int64_t sys_firewall_op(uint32_t cmd, uint64_t arg1, uint64_t arg2, uint64_t arg3);

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

/* Address Parsing & Formatting */
uint32_t    inet_addr(const char *cp);
char       *inet_ntoa(struct in_addr in);
const char *inet_ntop(int af, const void *src, char *dst, size_t size);
int         inet_pton(int af, const char *src, void *dst);

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

/* Time Subsystem Types & Constants */
typedef int64_t time_t;

#ifndef _STRUCT_TIMESPEC_DEFINED
#define _STRUCT_TIMESPEC_DEFINED
struct timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};
typedef struct timespec timespec_t;
#endif

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

#define REBOOT_CMD_HALT     0
#define REBOOT_CMD_REBOOT   1
#define REBOOT_CMD_POWEROFF 2

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

/* Phase 9 System Calls */
time_t  time(time_t *tloc);
int     clock_gettime(int clk_id, struct timespec *tp);
int     nanosleep(const struct timespec *req, struct timespec *rem);
int     chdir(const char *path);
char   *getcwd(char *buf, size_t size);
int     sync(void);
int     reboot(int cmd);
int     klog(int action, char *buf, size_t len);
int     sysinfo(sysinfo_data_t *info);
int     tty_ctrl(int cmd, uint64_t arg);
int     execve(const char *path, char *const argv[], char *const envp[]);
int     setpgid(int pid, int pgid);
int     getpgid(int pid);
int     setsid(void);
int     getsid(int pid);
int     unlink(const char *path);

/* Framebuffer Information */
#define FB_FORMAT_RGB888    1
#define FB_FORMAT_BGR888    2
#define FB_FORMAT_ARGB8888  3
#define FB_FORMAT_RGBA8888  4

typedef struct {
    uint64_t address;
    uint64_t phys_addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t format;
    uint32_t size;
    bool     initialized;
} fb_info_t;

/* Input Events */
typedef enum {
    EVENT_NONE                   = 0,
    EVENT_KEY_PRESS              = 1,
    EVENT_KEY_RELEASE            = 2,
    EVENT_MOUSE_MOVE             = 3,
    EVENT_MOUSE_BUTTON_PRESS     = 4,
    EVENT_MOUSE_BUTTON_RELEASE   = 5,
    EVENT_WINDOW_CLOSE           = 6,
    EVENT_WINDOW_FOCUS           = 7,
    EVENT_WINDOW_RESIZE          = 8
} input_event_type_t;

#define MOUSE_BTN_LEFT   (1 << 0)
#define MOUSE_BTN_RIGHT  (1 << 1)
#define MOUSE_BTN_MIDDLE (1 << 2)

typedef struct {
    uint32_t type;
    uint32_t keycode;
    char     character;
    int32_t  mouse_x;
    int32_t  mouse_y;
    int32_t  mouse_dx;
    int32_t  mouse_dy;
    uint32_t mouse_buttons;
    uint32_t modifiers;
    uint32_t window_id;
    uint64_t timestamp;
} __attribute__((packed)) input_event_t;

int gfx_get_info(fb_info_t *info);
int gfx_flip(const void *buf, size_t size);
int input_poll_event(input_event_t *ev, int blocking);

/* Window Management System Calls */
#define WIN_TITLE_MAX           64
#define WIN_STATE_NORMAL        0
#define WIN_STATE_MINIMIZED     1
#define WIN_STATE_MAXIMIZED     2
#define WIN_STATE_CLOSED        3

#define WS_OP_REGISTER          1
#define WS_OP_GET_WINDOWS       2
#define WS_OP_READ_PIXELS       3
#define WS_OP_SET_WINDOW_PROP   4
#define WS_OP_POST_EVENT        5

typedef struct {
    uint32_t id;
    uint32_t owner_pid;
    int32_t  x;
    int32_t  y;
    uint32_t width;
    uint32_t height;
    char     title[WIN_TITLE_MAX];
    bool     visible;
    bool     focused;
    uint32_t z_order;
    uint32_t state;
    bool     dirty;
    uint32_t dirty_x, dirty_y, dirty_w, dirty_h;
} window_info_t;

int win_create(uint32_t width, uint32_t height, const char *title);
int win_destroy(uint32_t win_id);
int win_update(uint32_t win_id, const void *pixels, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
int win_get_event(uint32_t win_id, input_event_t *ev, int block);
int win_server_op(uint32_t op, void *a1, void *a2, void *a3);

/* Environment Variables */
extern char **environ;
char *getenv(const char *name);
int   setenv(const char *name, const char *value, int overwrite);
int   unsetenv(const char *name);

/* Dynamic Memory Allocation & Heap */
void *malloc(size_t size);
void  free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
int   brk(void *addr);
void *sbrk(intptr_t increment);

/* String & Memory Functions */
size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
char *strdup(const char *s);
char *strndup(const char *s, size_t n);
char *strtok(char *str, const char *delim);
char *strtok_r(char *str, const char *delim, char **saveptr);
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);
int   atoi(const char *s);
long  atol(const char *s);
long  strtol(const char *nptr, char **endptr, int base);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int   memcmp(const void *s1, const void *s2, size_t n);

/* Character Classification & Conversion (ctype) */
int isspace(int c);
int isdigit(int c);
int isalpha(int c);
int isalnum(int c);
int isprint(int c);
int isupper(int c);
int islower(int c);
int toupper(int c);
int tolower(int c);

/* Standard I/O Functions */
#ifndef _VA_LIST_DEFINED
#define _VA_LIST_DEFINED
typedef __builtin_va_list va_list;
#define va_start(v, l)  __builtin_va_start(v, l)
#define va_end(v)       __builtin_va_end(v)
#define va_arg(v, l)    __builtin_va_arg(v, l)
#define va_copy(d, s)   __builtin_va_copy(d, s)
#endif

int putchar(int c);
int puts(const char *s);
int getchar(void);
int getline(char *buf, size_t size);
int printf(const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);
int fprintf(int fd, const char *format, ...);
int dprintf(int fd, const char *format, ...);

#endif /* LIBNYOTA_H */
