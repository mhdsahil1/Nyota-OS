/* =============================================================================
 * Nyota OS — Userspace Syscall Wrappers (syscall.c)
 * Triggers Vector 0x80 kernel traps conforming to the Nyota Syscall ABI.
 * Uses explicit register variables for 100% reliable System V parameter binding.
 * =========================================================================== */

#include "libnyota.h"

static inline int64_t syscall0(uint64_t num) {
    register uint64_t r_rax __asm__("rax") = num;
    __asm__ volatile (
        "int $0x80"
        : "+r"(r_rax)
        :
        : "rcx", "r11", "memory"
    );
    return (int64_t)r_rax;
}

static inline int64_t syscall1(uint64_t num, uint64_t a1) {
    register uint64_t r_rax __asm__("rax") = num;
    register uint64_t r_rdi __asm__("rdi") = a1;
    __asm__ volatile (
        "int $0x80"
        : "+r"(r_rax)
        : "r"(r_rdi)
        : "rcx", "r11", "memory"
    );
    return (int64_t)r_rax;
}

static inline int64_t syscall2(uint64_t num, uint64_t a1, uint64_t a2) {
    register uint64_t r_rax __asm__("rax") = num;
    register uint64_t r_rdi __asm__("rdi") = a1;
    register uint64_t r_rsi __asm__("rsi") = a2;
    __asm__ volatile (
        "int $0x80"
        : "+r"(r_rax)
        : "r"(r_rdi), "r"(r_rsi)
        : "rcx", "r11", "memory"
    );
    return (int64_t)r_rax;
}

static inline int64_t syscall3(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3) {
    register uint64_t r_rax __asm__("rax") = num;
    register uint64_t r_rdi __asm__("rdi") = a1;
    register uint64_t r_rsi __asm__("rsi") = a2;
    register uint64_t r_rdx __asm__("rdx") = a3;
    __asm__ volatile (
        "int $0x80"
        : "+r"(r_rax)
        : "r"(r_rdi), "r"(r_rsi), "r"(r_rdx)
        : "rcx", "r11", "memory"
    );
    return (int64_t)r_rax;
}

static inline int64_t syscall4(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    register uint64_t r_rax __asm__("rax") = num;
    register uint64_t r_rdi __asm__("rdi") = a1;
    register uint64_t r_rsi __asm__("rsi") = a2;
    register uint64_t r_rdx __asm__("rdx") = a3;
    register uint64_t r_r10 __asm__("r10") = a4;
    __asm__ volatile (
        "int $0x80"
        : "+r"(r_rax)
        : "r"(r_rdi), "r"(r_rsi), "r"(r_rdx), "r"(r_r10)
        : "rcx", "r11", "memory"
    );
    return (int64_t)r_rax;
}

static inline int64_t syscall6(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    register uint64_t r_rax __asm__("rax") = num;
    register uint64_t r_rdi __asm__("rdi") = a1;
    register uint64_t r_rsi __asm__("rsi") = a2;
    register uint64_t r_rdx __asm__("rdx") = a3;
    register uint64_t r_r10 __asm__("r10") = a4;
    register uint64_t r_r8  __asm__("r8")  = a5;
    register uint64_t r_r9  __asm__("r9")  = a6;
    __asm__ volatile (
        "int $0x80"
        : "+r"(r_rax)
        : "r"(r_rdi), "r"(r_rsi), "r"(r_rdx), "r"(r_r10), "r"(r_r8), "r"(r_r9)
        : "rcx", "r11", "memory"
    );
    return (int64_t)r_rax;
}

int64_t write(int fd, const void *buf, size_t count) {
    return syscall3(0, (uint64_t)fd, (uint64_t)buf, (uint64_t)count);
}

void exit(int status) {
    syscall1(1, (uint64_t)status);
    while (1) {
        __asm__ volatile ("hlt");
    }
}

int getpid(void) {
    return (int)syscall0(2);
}

void yield(void) {
    syscall0(3);
}

int sleep(uint64_t ms) {
    return (int)syscall1(4, ms);
}

int open(const char *path, int flags, ...) {
    return (int)syscall3(5, (uint64_t)path, (uint64_t)flags, 0755);
}

int close(int fd) {
    return (int)syscall1(6, (uint64_t)fd);
}

int64_t read(int fd, void *buf, size_t count) {
    return syscall3(7, (uint64_t)fd, (uint64_t)buf, (uint64_t)count);
}

int64_t seek(int fd, int64_t offset, int whence) {
    return syscall3(8, (uint64_t)fd, (uint64_t)offset, (uint64_t)whence);
}

int stat(const char *path, stat_t *st) {
    return (int)syscall2(9, (uint64_t)path, (uint64_t)st);
}

int getdents(int fd, dirent_t *dirp) {
    return (int)syscall3(10, (uint64_t)fd, (uint64_t)dirp, 1);
}

int mkdir(const char *path, int mode) {
    return (int)syscall2(11, (uint64_t)path, (uint64_t)mode);
}

int exec(const char *path, char *const argv[]) {
    return (int)syscall2(13, (uint64_t)path, (uint64_t)argv);
}

int spawn(const char *path, char *const argv[]) {
    return (int)syscall2(14, (uint64_t)path, (uint64_t)argv);
}

#undef waitpid
int waitpid(int pid, int *status) {
    return (int)syscall3(15, (uint64_t)pid, (uint64_t)status, 0);
}

int waitpid_options(int pid, int *status, int options) {
    return (int)syscall3(15, (uint64_t)pid, (uint64_t)status, (uint64_t)options);
}

int spawn2(const char *path, char *const argv[], int in_fd, int out_fd) {
    return (int)syscall6(41, (uint64_t)path, (uint64_t)argv, (uint64_t)in_fd, (uint64_t)out_fd, (uint64_t)environ, 0);
}

int pipe(int fds[2]) {
    return (int)syscall1(26, (uint64_t)fds);
}

int dup2(int oldfd, int newfd) {
    return (int)syscall2(27, (uint64_t)oldfd, (uint64_t)newfd);
}

int kill(int pid, int sig) {
    return (int)syscall2(28, (uint64_t)pid, (uint64_t)sig);
}

int signal(int sig, void (*handler)(int)) {
    return (int)syscall2(29, (uint64_t)sig, (uint64_t)handler);
}

uint32_t getuid(void) {
    return (uint32_t)syscall0(30);
}

int setuid(uint32_t uid) {
    return (int)syscall1(31, (uint64_t)uid);
}

uint32_t getgid(void) {
    return (uint32_t)syscall0(32);
}

int setgid(uint32_t gid) {
    return (int)syscall1(33, (uint64_t)gid);
}

int chmod(const char *path, uint32_t mode) {
    return (int)syscall2(34, (uint64_t)path, (uint64_t)mode);
}

int chown(const char *path, uint32_t uid, uint32_t gid) {
    return (int)syscall3(35, (uint64_t)path, (uint64_t)uid, (uint64_t)gid);
}

int shm_get(uint32_t key, size_t size, int flags) {
    return (int)syscall3(36, (uint64_t)key, (uint64_t)size, (uint64_t)flags);
}

void *shm_at(int shmid, const void *addr, int flags) {
    return (void *)syscall3(37, (uint64_t)shmid, (uint64_t)addr, (uint64_t)flags);
}

int shm_dt(const void *addr) {
    return (int)syscall1(38, (uint64_t)addr);
}

int shm_ctl(int shmid, int cmd, void *buf) {
    return (int)syscall3(39, (uint64_t)shmid, (uint64_t)cmd, (uint64_t)buf);
}

int64_t getrandom(void *buf, size_t len, unsigned int flags) {
    return syscall3(40, (uint64_t)buf, (uint64_t)len, (uint64_t)flags);
}

int secinfo(secinfo_t *info) {
    return (int)syscall1(42, (uint64_t)info);
}

int getprocs(proc_info_t *buf, size_t max_count) {
    return (int)syscall2(43, (uint64_t)buf, max_count);
}

int socket(int domain, int type, int protocol) {
    return (int)syscall3(16, (uint64_t)domain, (uint64_t)type, (uint64_t)protocol);
}

int bind(int fd, const struct sockaddr *addr, size_t addrlen) {
    return (int)syscall3(17, (uint64_t)fd, (uint64_t)addr, addrlen);
}

int listen(int fd, int backlog) {
    return (int)syscall2(18, (uint64_t)fd, (uint64_t)backlog);
}

int accept(int fd, struct sockaddr *addr, size_t *addrlen) {
    return (int)syscall3(19, (uint64_t)fd, (uint64_t)addr, (uint64_t)addrlen);
}

int connect(int fd, const struct sockaddr *addr, size_t addrlen) {
    return (int)syscall3(20, (uint64_t)fd, (uint64_t)addr, addrlen);
}

int64_t send(int fd, const void *buf, size_t len, int flags) {
    return syscall4(21, (uint64_t)fd, (uint64_t)buf, len, (uint64_t)flags);
}

int64_t recv(int fd, void *buf, size_t len, int flags) {
    return syscall4(22, (uint64_t)fd, (uint64_t)buf, len, (uint64_t)flags);
}

int64_t sendto(int fd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, size_t addrlen) {
    return syscall6(23, (uint64_t)fd, (uint64_t)buf, len, (uint64_t)flags, (uint64_t)dest_addr, addrlen);
}

int64_t recvfrom(int fd, void *buf, size_t len, int flags, struct sockaddr *src_addr, size_t *addrlen) {
    return syscall6(24, (uint64_t)fd, (uint64_t)buf, len, (uint64_t)flags, (uint64_t)src_addr, (uint64_t)addrlen);
}

int shutdown(int fd, int how) {
    return (int)syscall2(25, (uint64_t)fd, (uint64_t)how);
}

uint32_t inet_addr(const char *cp) {
    if (!cp) return 0;
    uint32_t parts[4] = {0};
    int idx = 0;

    while (*cp && idx < 4) {
        if (*cp >= '0' && *cp <= '9') {
            parts[idx] = parts[idx] * 10 + (*cp - '0');
        } else if (*cp == '.') {
            idx++;
        } else {
            break;
        }
        cp++;
    }

    if (idx != 3) return 0;

    return ((parts[0] & 0xFF) |
           ((parts[1] & 0xFF) << 8) |
           ((parts[2] & 0xFF) << 16) |
           ((parts[3] & 0xFF) << 24));
}

static char inet_ntoa_buf[32];

char *inet_ntoa(struct in_addr in) {
    uint32_t ip = in.s_addr;
    uint8_t b1 = ip & 0xFF;
    uint8_t b2 = (ip >> 8) & 0xFF;
    uint8_t b3 = (ip >> 16) & 0xFF;
    uint8_t b4 = (ip >> 24) & 0xFF;

    char *p = inet_ntoa_buf;
    uint8_t bytes[4] = {b1, b2, b3, b4};
    for (int i = 0; i < 4; i++) {
        uint8_t val = bytes[i];
        if (val >= 100) {
            *p++ = (char)('0' + (val / 100));
            *p++ = (char)('0' + ((val / 10) % 10));
            *p++ = (char)('0' + (val % 10));
        } else if (val >= 10) {
            *p++ = (char)('0' + (val / 10));
            *p++ = (char)('0' + (val % 10));
        } else {
            *p++ = (char)('0' + val);
        }
        if (i < 3) *p++ = '.';
    }
    *p = '\0';
    return inet_ntoa_buf;
}

time_t time(time_t *tloc) {
    return (time_t)syscall1(44, (uint64_t)tloc);
}

int clock_gettime(int clk_id, struct timespec *tp) {
    return (int)syscall2(45, (uint64_t)clk_id, (uint64_t)tp);
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
    return (int)syscall2(46, (uint64_t)req, (uint64_t)rem);
}

int chdir(const char *path) {
    return (int)syscall1(47, (uint64_t)path);
}

char *getcwd(char *buf, size_t size) {
    int64_t res = syscall2(48, (uint64_t)buf, size);
    if (res < 0) return NULL;
    return buf;
}

int sync(void) {
    return (int)syscall0(49);
}

int reboot(int cmd) {
    return (int)syscall1(50, (uint64_t)cmd);
}

int klog(int action, char *buf, size_t len) {
    return (int)syscall3(51, (uint64_t)action, (uint64_t)buf, len);
}

int sysinfo(sysinfo_data_t *info) {
    return (int)syscall1(52, (uint64_t)info);
}

int tty_ctrl(int cmd, uint64_t arg) {
    return (int)syscall2(53, (uint64_t)cmd, arg);
}

int execve(const char *path, char *const argv[], char *const envp[]) {
    return (int)syscall3(54, (uint64_t)path, (uint64_t)argv, (uint64_t)envp);
}

int setpgid(int pid, int pgid) {
    return (int)syscall2(55, (uint64_t)pid, (uint64_t)pgid);
}

int getpgid(int pid) {
    return (int)syscall1(56, (uint64_t)pid);
}

int setsid(void) {
    return (int)syscall0(57);
}

int getsid(int pid) {
    return (int)syscall1(58, (uint64_t)pid);
}

int unlink(const char *path) {
    return (int)syscall1(59, (uint64_t)path);
}
