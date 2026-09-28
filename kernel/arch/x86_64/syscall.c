/* =============================================================================
 * Nyota OS — System Call Implementation & Dispatcher (Phase 6)
 * Handles vector 0x80 syscalls, enforces user memory validation, routes to VFS.
 * =========================================================================== */

#include "syscall.h"
#include "process.h"
#include "paging.h"
#include "idt.h"
#include "vga.h"
#include "serial.h"
#include "memory.h"
#include "kernel.h"
#include "scheduler.h"
#include "fs/vfs.h"
#include "net/socket.h"

/* ── User Memory Validation ───────────────────────────────────────────────── */

bool user_validate_pointer(const void *uptr, size_t len, bool write) {
    (void)write;
    if (!uptr) return false;

    uint64_t addr = (uint64_t)uptr;

    /* Verify address falls strictly within user space (512 GB window) */
    if (addr < USER_SPACE_BASE || addr >= USER_SPACE_END) {
        return false;
    }
    if (addr + len > USER_SPACE_END || addr + len < addr) {
        return false;
    }

    process_t *proc = process_get_current();
    page_table_t *pml4 = proc ? (page_table_t *)proc->cr3 : NULL;
    if (!pml4) {
        uint64_t cr3 = paging_read_cr3();
        pml4 = (page_table_t *)(cr3 & PAGE_ENTRY_ADDR_MASK);
    }

    uint64_t start_page = PAGE_ALIGN_DOWN(addr);
    uint64_t end_page = PAGE_ALIGN_UP(addr + len);

    for (uint64_t p = start_page; p < end_page; p += PAGE_SIZE) {
        if (paging_get_physical_in(pml4, p) == 0) {
            return false;
        }
    }

    return true;
}

int copy_from_user(void *kdest, const void *usrc, size_t max_len) {
    if (!kdest || !usrc) return SYS_ERR_EFAULT;
    if (!user_validate_pointer(usrc, max_len, false)) {
        return SYS_ERR_EFAULT;
    }

    const uint8_t *s = (const uint8_t *)usrc;
    uint8_t *d = (uint8_t *)kdest;

    for (size_t i = 0; i < max_len; i++) {
        d[i] = s[i];
    }
    return (int)max_len;
}

int copy_to_user(void *udest, const void *ksrc, size_t len) {
    if (!udest || !ksrc) return SYS_ERR_EFAULT;
    if (!user_validate_pointer(udest, len, true)) {
        return SYS_ERR_EFAULT;
    }

    const uint8_t *s = (const uint8_t *)ksrc;
    uint8_t *d = (uint8_t *)udest;

    for (size_t i = 0; i < len; i++) {
        d[i] = s[i];
    }
    return (int)len;
}

int copy_string_from_user(char *kdest, const char *usrc, size_t max_len) {
    if (!kdest || !usrc || max_len == 0) return SYS_ERR_EFAULT;

    size_t i = 0;
    while (i < max_len - 1) {
        if (!user_validate_pointer(usrc + i, 1, false)) {
            return SYS_ERR_EFAULT;
        }
        char c = usrc[i];
        kdest[i] = c;
        if (c == '\0') {
            return (int)i;
        }
        i++;
    }
    kdest[i] = '\0';
    return (int)i;
}

/* ── Syscall Handlers ─────────────────────────────────────────────────────── */

static int64_t sys_handle_write(uint64_t a1, uint64_t a2, uint64_t a3) {
    /* Compatibility with Phase 5 test blobs (where RDI was buffer pointer, RSI was length) */
    if (a1 >= USER_SPACE_BASE) {
        const void *buf = (const void *)a1;
        size_t len = (size_t)a2;
        if (!user_validate_pointer(buf, len, false)) {
            return SYS_ERR_EFAULT;
        }
        return vfs_write(STDOUT_FILENO, buf, len);
    }

    int fd = (int)a1;
    const void *buf = (const void *)a2;
    size_t count = (size_t)a3;

    if (!user_validate_pointer(buf, count, false)) {
        serial_write("[SYS_WRITE EFAULT] fd="); serial_write_dec(fd);
        serial_write(" buf="); serial_write_hex((uint64_t)buf);
        serial_write(" count="); serial_write_dec(count); serial_write("\n");
        return SYS_ERR_EFAULT;
    }

    return vfs_write(fd, buf, count);

}

static int64_t sys_handle_exit(int status) {
    process_exit(status);
    return 0;
}

static int64_t sys_handle_getpid(void) {
    process_t *curr = process_get_current();
    return curr ? curr->pid : 0;
}

static int64_t sys_handle_yield(void) {
    scheduler_yield();
    return 0;
}

static int64_t sys_handle_sleep(uint64_t ms) {
    scheduler_sleep(ms);
    return 0;
}

static int64_t sys_handle_open(uint64_t path_uptr, uint64_t flags, uint64_t mode) {
    char kpath[256];
    if (copy_string_from_user(kpath, (const char *)path_uptr, sizeof(kpath)) < 0) {
        return SYS_ERR_EFAULT;
    }
    return vfs_open(kpath, (int)flags, (int)mode);
}

static int64_t sys_handle_close(uint64_t fd) {
    return vfs_close((int)fd);
}

static int64_t sys_handle_read(uint64_t fd, uint64_t buf_uptr, uint64_t count) {
    if (!user_validate_pointer((const void *)buf_uptr, (size_t)count, true)) {
        return SYS_ERR_EFAULT;
    }
    return vfs_read((int)fd, (void *)buf_uptr, (size_t)count);
}

static int64_t sys_handle_seek(uint64_t fd, uint64_t offset, uint64_t whence) {
    return vfs_seek((int)fd, (int64_t)offset, (int)whence);
}

static int64_t sys_handle_stat(uint64_t path_uptr, uint64_t st_uptr) {
    char kpath[256];
    if (copy_string_from_user(kpath, (const char *)path_uptr, sizeof(kpath)) < 0) {
        return SYS_ERR_EFAULT;
    }
    if (!user_validate_pointer((const void *)st_uptr, sizeof(vfs_stat_t), true)) {
        return SYS_ERR_EFAULT;
    }

    vfs_stat_t kst;
    int res = vfs_stat(kpath, &kst);
    if (res == 0) {
        copy_to_user((void *)st_uptr, &kst, sizeof(vfs_stat_t));
    }
    return res;
}

static int64_t sys_handle_getdents(uint64_t fd, uint64_t dirp_uptr, uint64_t count) {
    (void)count;
    if (!user_validate_pointer((const void *)dirp_uptr, sizeof(vfs_dirent_t), true)) {
        return SYS_ERR_EFAULT;
    }

    vfs_dirent_t kdirp;
    int res = vfs_readdir((int)fd, &kdirp);
    if (res > 0) {
        copy_to_user((void *)dirp_uptr, &kdirp, sizeof(vfs_dirent_t));
        return 1;
    }
    return res; /* 0 for EOF, negative on error */
}

static int64_t sys_handle_mkdir(uint64_t path_uptr, uint64_t mode) {
    char kpath[256];
    if (copy_string_from_user(kpath, (const char *)path_uptr, sizeof(kpath)) < 0) {
        return SYS_ERR_EFAULT;
    }
    return vfs_mkdir(kpath, (int)mode);
}

static int64_t sys_handle_create(uint64_t path_uptr, uint64_t mode) {
    char kpath[256];
    if (copy_string_from_user(kpath, (const char *)path_uptr, sizeof(kpath)) < 0) {
        return SYS_ERR_EFAULT;
    }
    return vfs_open(kpath, O_CREAT | O_WRONLY | O_TRUNC, (int)mode);
}

static int64_t sys_handle_exec(uint64_t path_uptr, uint64_t argv_uptr) {
    char kpath[256];
    if (copy_string_from_user(kpath, (const char *)path_uptr, sizeof(kpath)) < 0) {
        return SYS_ERR_EFAULT;
    }

    char *kargv[32] = {0};
    char arg_bufs[32][64];

    if (argv_uptr != 0) {
        const char **uargv = (const char **)argv_uptr;
        for (int i = 0; i < 31; i++) {
            if (!user_validate_pointer(&uargv[i], sizeof(char *), false)) break;
            const char *uarg = uargv[i];
            if (!uarg) break;
            if (copy_string_from_user(arg_bufs[i], uarg, sizeof(arg_bufs[i])) < 0) break;
            kargv[i] = arg_bufs[i];
        }
    }

    return process_exec(process_get_current(), kpath, kargv);
}

static int64_t sys_handle_spawn(uint64_t path_uptr, uint64_t argv_uptr) {
    char kpath[256];
    if (copy_string_from_user(kpath, (const char *)path_uptr, sizeof(kpath)) < 0) {
        return SYS_ERR_EFAULT;
    }

    char *kargv[32] = {0};
    char arg_bufs[32][64];

    if (argv_uptr != 0) {
        const char **uargv = (const char **)argv_uptr;
        for (int i = 0; i < 31; i++) {
            if (!user_validate_pointer(&uargv[i], sizeof(char *), false)) break;
            const char *uarg = uargv[i];
            if (!uarg) break;
            if (copy_string_from_user(arg_bufs[i], uarg, sizeof(arg_bufs[i])) < 0) break;
            kargv[i] = arg_bufs[i];
        }
    }

    process_t *child = process_spawn_elf(kpath, kargv);
    if (!child) return SYS_ERR_ENOENT;
    return (int64_t)child->pid;
}

static int64_t sys_handle_waitpid(uint64_t pid, uint64_t status_uptr) {
    int kstatus = 0;
    int res = process_waitpid((uint32_t)pid, &kstatus);
    if (res > 0 && status_uptr != 0) {
        if (user_validate_pointer((const void *)status_uptr, sizeof(int), true)) {
            copy_to_user((void *)status_uptr, &kstatus, sizeof(int));
        }
    }
    return res;
}

/* ── Socket System Call Handlers ─────────────────────────────────────────── */

static int64_t sys_handle_socket(int domain, int type, int protocol) {
    socket_t *sock = socket_create(domain, type, protocol);
    if (!sock) return SYS_ERR_ENOMEM;

    file_t *f = vfs_create_socket_file(sock);
    if (!f) {
        socket_close(sock);
        return SYS_ERR_ENOMEM;
    }

    process_t *curr = process_get_current();
    if (!curr) return SYS_ERR_EBADF;

    int fd = vfs_alloc_fd(curr->fds, f);
    if (fd < 0) {
        return SYS_ERR_ENOSPC;
    }

    return fd;
}

static int64_t sys_handle_bind(int fd, uint64_t addr_uptr, size_t addrlen) {
    (void)addrlen;
    file_t *f = vfs_get_file(fd);
    if (!f || f->type != FILE_TYPE_SOCKET || !f->filesystem_data) return SYS_ERR_EBADF;

    struct sockaddr_in kaddr;
    if (copy_from_user(&kaddr, (const void *)addr_uptr, sizeof(kaddr)) < 0) {
        return SYS_ERR_EFAULT;
    }

    return socket_bind((socket_t *)f->filesystem_data, &kaddr);
}

static int64_t sys_handle_listen(int fd, int backlog) {
    file_t *f = vfs_get_file(fd);
    if (!f || f->type != FILE_TYPE_SOCKET || !f->filesystem_data) return SYS_ERR_EBADF;

    return socket_listen((socket_t *)f->filesystem_data, backlog);
}

static int64_t sys_handle_accept(int fd, uint64_t addr_uptr, uint64_t addrlen_uptr) {
    (void)addrlen_uptr;
    file_t *f = vfs_get_file(fd);
    if (!f || f->type != FILE_TYPE_SOCKET || !f->filesystem_data) return SYS_ERR_EBADF;

    struct sockaddr_in kaddr;
    socket_t *client_sock = socket_accept((socket_t *)f->filesystem_data, &kaddr);
    if (!client_sock) return SYS_ERR_EIO;

    file_t *client_f = vfs_create_socket_file(client_sock);
    if (!client_f) {
        socket_close(client_sock);
        return SYS_ERR_ENOMEM;
    }

    process_t *curr = process_get_current();
    int new_fd = vfs_alloc_fd(curr->fds, client_f);
    if (new_fd < 0) {
        return SYS_ERR_ENOSPC;
    }

    if (addr_uptr != 0) {
        if (user_validate_pointer((void *)addr_uptr, sizeof(kaddr), true)) {
            copy_to_user((void *)addr_uptr, &kaddr, sizeof(kaddr));
        }
    }

    return new_fd;
}

static int64_t sys_handle_connect(int fd, uint64_t addr_uptr, size_t addrlen) {
    (void)addrlen;
    file_t *f = vfs_get_file(fd);
    if (!f || f->type != FILE_TYPE_SOCKET || !f->filesystem_data) return SYS_ERR_EBADF;

    struct sockaddr_in kaddr;
    if (copy_from_user(&kaddr, (const void *)addr_uptr, sizeof(kaddr)) < 0) {
        return SYS_ERR_EFAULT;
    }

    int ret = socket_connect((socket_t *)f->filesystem_data, &kaddr);
    return (ret == 0) ? 0 : SYS_ERR_ECONNREFUSED;
}

static int64_t sys_handle_send(int fd, uint64_t buf_uptr, size_t len, int flags) {
    file_t *f = vfs_get_file(fd);
    if (!f || f->type != FILE_TYPE_SOCKET || !f->filesystem_data) return SYS_ERR_EBADF;

    if (!user_validate_pointer((const void *)buf_uptr, len, false)) {
        return SYS_ERR_EFAULT;
    }

    return socket_send((socket_t *)f->filesystem_data, (const void *)buf_uptr, len, flags);
}

static int64_t sys_handle_recv(int fd, uint64_t buf_uptr, size_t len, int flags) {
    file_t *f = vfs_get_file(fd);
    if (!f || f->type != FILE_TYPE_SOCKET || !f->filesystem_data) return SYS_ERR_EBADF;

    if (!user_validate_pointer((void *)buf_uptr, len, true)) {
        return SYS_ERR_EFAULT;
    }

    return socket_recv((socket_t *)f->filesystem_data, (void *)buf_uptr, len, flags);
}

static int64_t sys_handle_sendto(int fd, uint64_t buf_uptr, size_t len, int flags, uint64_t dest_uptr, size_t addrlen) {
    (void)addrlen;
    file_t *f = vfs_get_file(fd);
    if (!f || f->type != FILE_TYPE_SOCKET || !f->filesystem_data) return SYS_ERR_EBADF;

    if (!user_validate_pointer((const void *)buf_uptr, len, false)) {
        return SYS_ERR_EFAULT;
    }

    struct sockaddr_in kdest;
    if (copy_from_user(&kdest, (const void *)dest_uptr, sizeof(kdest)) < 0) {
        return SYS_ERR_EFAULT;
    }

    return socket_sendto((socket_t *)f->filesystem_data, (const void *)buf_uptr, len, flags, &kdest);
}

static int64_t sys_handle_recvfrom(int fd, uint64_t buf_uptr, size_t len, int flags, uint64_t src_uptr, uint64_t addrlen_uptr) {
    (void)addrlen_uptr;
    file_t *f = vfs_get_file(fd);
    if (!f || f->type != FILE_TYPE_SOCKET || !f->filesystem_data) return SYS_ERR_EBADF;

    if (!user_validate_pointer((void *)buf_uptr, len, true)) {
        return SYS_ERR_EFAULT;
    }

    struct sockaddr_in ksrc;
    int64_t ret = socket_recvfrom((socket_t *)f->filesystem_data, (void *)buf_uptr, len, flags, &ksrc);

    if (ret >= 0 && src_uptr != 0) {
        if (user_validate_pointer((void *)src_uptr, sizeof(ksrc), true)) {
            copy_to_user((void *)src_uptr, &ksrc, sizeof(ksrc));
        }
    }

    return ret;
}

static int64_t sys_handle_shutdown(int fd, int how) {
    (void)how;
    file_t *f = vfs_get_file(fd);
    if (!f || f->type != FILE_TYPE_SOCKET || !f->filesystem_data) return SYS_ERR_EBADF;

    return socket_close((socket_t *)f->filesystem_data);
}

/* ── Syscall Dispatcher ───────────────────────────────────────────────────── */

int64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    switch (num) {
        case SYS_WRITE:    return sys_handle_write(a1, a2, a3);
        case SYS_EXIT:     return sys_handle_exit((int)a1);
        case SYS_GETPID:   return sys_handle_getpid();
        case SYS_YIELD:    return sys_handle_yield();
        case SYS_SLEEP:    return sys_handle_sleep(a1);
        case SYS_OPEN:     return sys_handle_open(a1, a2, a3);
        case SYS_CLOSE:    return sys_handle_close(a1);
        case SYS_READ:     return sys_handle_read(a1, a2, a3);
        case SYS_SEEK:     return sys_handle_seek(a1, a2, a3);
        case SYS_STAT:     return sys_handle_stat(a1, a2);
        case SYS_GETDENTS: return sys_handle_getdents(a1, a2, a3);
        case SYS_MKDIR:    return sys_handle_mkdir(a1, a2);
        case SYS_CREATE:   return sys_handle_create(a1, a2);
        case SYS_EXEC:     return sys_handle_exec(a1, a2);
        case SYS_SPAWN:    return sys_handle_spawn(a1, a2);
        case SYS_WAITPID:  return sys_handle_waitpid(a1, a2);
        case SYS_SOCKET:   return sys_handle_socket((int)a1, (int)a2, (int)a3);
        case SYS_BIND:     return sys_handle_bind((int)a1, a2, (size_t)a3);
        case SYS_LISTEN:   return sys_handle_listen((int)a1, (int)a2);
        case SYS_ACCEPT:   return sys_handle_accept((int)a1, a2, a3);
        case SYS_CONNECT:  return sys_handle_connect((int)a1, a2, (size_t)a3);
        case SYS_SEND:     return sys_handle_send((int)a1, a2, (size_t)a3, (int)a4);
        case SYS_RECV:     return sys_handle_recv((int)a1, a2, (size_t)a3, (int)a4);
        case SYS_SENDTO:   return sys_handle_sendto((int)a1, a2, (size_t)a3, (int)a4, a5, (size_t)a6);
        case SYS_RECVFROM: return sys_handle_recvfrom((int)a1, a2, (size_t)a3, (int)a4, a5, a6);
        case SYS_SHUTDOWN: return sys_handle_shutdown((int)a1, (int)a2);
        default:           return SYS_ERR_ENOSYS;
    }
}

void syscall_handler(interrupt_frame_t *frame) {
    if (!frame) return;

    int64_t ret = syscall_dispatch(
        frame->rax,
        frame->rdi,
        frame->rsi,
        frame->rdx,
        frame->r10,
        frame->r8,
        frame->r9
    );

    frame->rax = (uint64_t)ret;
}

/* ── Initialization ───────────────────────────────────────────────────────── */

extern uint64_t isr_stub_table[IDT_ENTRIES];

void syscall_init(void) {
    idt_set_gate(0x80, isr_stub_table[0x80], 0x08, IDT_GATE_USER_TRAP);
    interrupt_register_handler(0x80, syscall_handler);
}

/* ── User-Mode Invocation Wrappers ────────────────────────────────────────── */

int64_t sys_write(int fd, const void *buf, size_t len) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov %3, %%rdx\n"
        "mov $0, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"((uint64_t)fd), "r"(buf), "r"(len)
        : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory"
    );
    return ret;
}

void sys_exit(int status) {
    __asm__ volatile (
        "mov %0, %%rdi\n"
        "mov $1, %%rax\n"
        "int $0x80\n"
        :
        : "r"((uint64_t)status)
        : "rax", "rdi", "rcx", "r11", "memory"
    );
    while (1) {
        __asm__ volatile ("hlt");
    }
}

int32_t sys_getpid(void) {
    int64_t ret;
    __asm__ volatile (
        "mov $2, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        :
        : "rax", "rcx", "r11", "memory"
    );
    return (int32_t)ret;
}

void sys_yield(void) {
    __asm__ volatile (
        "mov $3, %%rax\n"
        "int $0x80\n"
        :
        :
        : "rax", "rcx", "r11", "memory"
    );
}

int64_t sys_sleep(uint64_t ms) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov $4, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"(ms)
        : "rax", "rdi", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_open(const char *path, int flags, int mode) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov %3, %%rdx\n"
        "mov $5, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"(path), "r"((uint64_t)flags), "r"((uint64_t)mode)
        : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_close(int fd) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov $6, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"((uint64_t)fd)
        : "rax", "rdi", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_read(int fd, void *buf, size_t len) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov %3, %%rdx\n"
        "mov $7, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"((uint64_t)fd), "r"(buf), "r"(len)
        : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_seek(int fd, int64_t offset, int whence) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov %3, %%rdx\n"
        "mov $8, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"((uint64_t)fd), "r"(offset), "r"((uint64_t)whence)
        : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_stat(const char *path, void *st) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov $9, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"(path), "r"(st)
        : "rax", "rdi", "rsi", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_getdents(int fd, void *dirp, size_t count) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov %3, %%rdx\n"
        "mov $10, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"((uint64_t)fd), "r"(dirp), "r"(count)
        : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_mkdir(const char *path, int mode) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov $11, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"(path), "r"((uint64_t)mode)
        : "rax", "rdi", "rsi", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_create(const char *path, int mode) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov $12, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"(path), "r"((uint64_t)mode)
        : "rax", "rdi", "rsi", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_exec(const char *path, char *const argv[]) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov $13, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"(path), "r"(argv)
        : "rax", "rdi", "rsi", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_spawn(const char *path, char *const argv[]) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov $14, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"(path), "r"(argv)
        : "rax", "rdi", "rsi", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_waitpid(uint32_t pid, int *status) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov $15, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"((uint64_t)pid), "r"(status)
        : "rax", "rdi", "rsi", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_socket(int domain, int type, int protocol) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov %3, %%rdx\n"
        "mov $16, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"((uint64_t)domain), "r"((uint64_t)type), "r"((uint64_t)protocol)
        : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_bind(int fd, const void *addr, size_t addrlen) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov %3, %%rdx\n"
        "mov $17, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"((uint64_t)fd), "r"(addr), "r"(addrlen)
        : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_listen(int fd, int backlog) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov $18, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"((uint64_t)fd), "r"((uint64_t)backlog)
        : "rax", "rdi", "rsi", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_accept(int fd, void *addr, void *addrlen) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov %3, %%rdx\n"
        "mov $19, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"((uint64_t)fd), "r"(addr), "r"(addrlen)
        : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_connect(int fd, const void *addr, size_t addrlen) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov %3, %%rdx\n"
        "mov $20, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"((uint64_t)fd), "r"(addr), "r"(addrlen)
        : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_send(int fd, const void *buf, size_t len, int flags) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov %3, %%rdx\n"
        "mov %4, %%r10\n"
        "mov $21, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"((uint64_t)fd), "r"(buf), "r"(len), "r"((uint64_t)flags)
        : "rax", "rdi", "rsi", "rdx", "r10", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_recv(int fd, void *buf, size_t len, int flags) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov %3, %%rdx\n"
        "mov %4, %%r10\n"
        "mov $22, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"((uint64_t)fd), "r"(buf), "r"(len), "r"((uint64_t)flags)
        : "rax", "rdi", "rsi", "rdx", "r10", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_sendto(int fd, const void *buf, size_t len, int flags, const void *dest, size_t addrlen) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov %3, %%rdx\n"
        "mov %4, %%r10\n"
        "mov %5, %%r8\n"
        "mov %6, %%r9\n"
        "mov $23, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"((uint64_t)fd), "r"(buf), "r"(len), "r"((uint64_t)flags), "r"(dest), "r"(addrlen)
        : "rax", "rdi", "rsi", "rdx", "r10", "r8", "r9", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_recvfrom(int fd, void *buf, size_t len, int flags, void *src, void *addrlen) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov %3, %%rdx\n"
        "mov %4, %%r10\n"
        "mov %5, %%r8\n"
        "mov %6, %%r9\n"
        "mov $24, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"((uint64_t)fd), "r"(buf), "r"(len), "r"((uint64_t)flags), "r"(src), "r"(addrlen)
        : "rax", "rdi", "rsi", "rdx", "r10", "r8", "r9", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_shutdown(int fd, int how) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov $25, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"((uint64_t)fd), "r"((uint64_t)how)
        : "rax", "rdi", "rsi", "rcx", "r11", "memory"
    );
    return ret;
}
