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

int waitpid(int pid, int *status) {
    while (1) {
        int64_t ret = syscall2(15, (uint64_t)pid, (uint64_t)status);
        if (ret != 0) {
            return (int)ret;
        }
        sleep(50);
    }
}
