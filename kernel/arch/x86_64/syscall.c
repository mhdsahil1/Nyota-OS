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
#include "signal.h"
#include "security/random.h"
#include "security/capability.h"
#include "security/security.h"
#include "ipc/pipe.h"
#include "ipc/shm.h"
#include "time/clock.h"
#include "time/rtc.h"
#include "drivers/tty.h"
#include "pmm.h"
#include "io.h"
#include "timer.h"

void ___chkstk_ms(void) {}

/* ── Integer Overflow & Validation Helpers ───────────────────────────────── */

bool size_add_overflow(size_t a, size_t b, size_t *result) {
    if (a > (size_t)-1 - b) return true;
    if (result) *result = a + b;
    return false;
}

bool size_mul_overflow(size_t a, size_t b, size_t *result) {
    if (a != 0 && b > (size_t)-1 / a) return true;
    if (result) *result = a * b;
    return false;
}

bool user_validate_string(const char *ustr, size_t max_len) {
    if (!ustr || max_len == 0) return false;
    for (size_t i = 0; i < max_len; i++) {
        if (!user_validate_pointer(ustr + i, 1, false)) return false;
        if (ustr[i] == '\0') return true;
    }
    return false;
}

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

static int64_t sys_handle_waitpid(uint64_t pid, uint64_t status_uptr, uint64_t options) {
    int kstatus = 0;
    int res = process_waitpid((int32_t)pid, &kstatus, (int)options);
    if (res > 0 && status_uptr != 0) {
        if (user_validate_pointer((const void *)status_uptr, sizeof(int), true)) {
            copy_to_user((void *)status_uptr, &kstatus, sizeof(int));
        }
    }
    return res;
}

/* ── Socket System Call Handlers ─────────────────────────────────────────── */

static int64_t sys_handle_socket(int domain, int type, int protocol) {
    process_t *curr = process_get_current();
    if (!curr) return SYS_ERR_EBADF;

    /* Raw sockets require CAP_NET_RAW capability */
    if (type == 3 /* SOCK_RAW */ && !has_capability(curr, CAP_NET_RAW)) {
        return SYS_ERR_EPERM;
    }

    /* Enforce per-process socket limits */
    if (curr->max_sockets > 0) {
        uint32_t sock_count = 0;
        for (int i = 0; i < MAX_PROCESS_FDS; i++) {
            if (curr->fds[i] && curr->fds[i]->type == FILE_TYPE_SOCKET) sock_count++;
        }
        if (sock_count >= curr->max_sockets) {
            return SYS_ERR_ENOSPC;
        }
    }

    socket_t *sock = socket_create(domain, type, protocol);
    if (!sock) return SYS_ERR_ENOMEM;

    file_t *f = vfs_create_socket_file(sock);
    if (!f) {
        socket_close(sock);
        return SYS_ERR_ENOMEM;
    }

    int fd = vfs_alloc_fd(curr->fds, f);
    if (fd < 0) {
        return SYS_ERR_ENOSPC;
    }

    return fd;
}

static int64_t sys_handle_bind(int fd, uint64_t addr_uptr, size_t addrlen) {
    file_t *f = vfs_get_file(fd);
    if (!f || f->type != FILE_TYPE_SOCKET || !f->filesystem_data) return SYS_ERR_EBADF;

    uint8_t kaddr_buf[sizeof(struct sockaddr_un)];
    size_t copy_len = (addrlen > sizeof(kaddr_buf) || addrlen == 0) ? sizeof(kaddr_buf) : addrlen;
    if (copy_from_user(kaddr_buf, (const void *)addr_uptr, copy_len) < 0) {
        return SYS_ERR_EFAULT;
    }

    return socket_bind((socket_t *)f->filesystem_data, (const struct sockaddr_in *)kaddr_buf);
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
    file_t *f = vfs_get_file(fd);
    if (!f || f->type != FILE_TYPE_SOCKET || !f->filesystem_data) return SYS_ERR_EBADF;

    uint8_t kaddr_buf[sizeof(struct sockaddr_un)];
    size_t copy_len = (addrlen > sizeof(kaddr_buf) || addrlen == 0) ? sizeof(kaddr_buf) : addrlen;
    if (copy_from_user(kaddr_buf, (const void *)addr_uptr, copy_len) < 0) {
        return SYS_ERR_EFAULT;
    }

    int ret = socket_connect((socket_t *)f->filesystem_data, (const struct sockaddr_in *)kaddr_buf);
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

/* ── Phase 8: IPC, Security, and Signals Handlers ────────────────────────── */

static int64_t sys_handle_pipe(uint64_t fds_uptr) {
    if (!user_validate_pointer((void *)fds_uptr, sizeof(int) * 2, true)) {
        return SYS_ERR_EFAULT;
    }
    int kfds[2] = {-1, -1};
    int res = pipe_alloc_pair(kfds);
    if (res != 0) return res;

    copy_to_user((void *)fds_uptr, kfds, sizeof(int) * 2);
    return 0;
}

static int64_t sys_handle_dup2(int oldfd, int newfd) {
    return vfs_dup2(oldfd, newfd);
}

static int64_t sys_handle_kill(int pid, int sig) {
    if (sig < 1 || sig > 31) return SYS_ERR_EINVAL;
    process_t *target = process_find((uint32_t)pid);
    if (!target) return SYS_ERR_ESRCH;

    process_t *curr = process_get_current();
    if (!can_signal_process(curr, target)) {
        return SYS_ERR_EPERM;
    }

    return signal_send(target, sig);
}

static int64_t sys_handle_signal(int sig, uint64_t handler_uptr) {
    if (sig < 1 || sig > 31) return SYS_ERR_EINVAL;
    if (sig == SIGKILL || sig == SIGSTOP) return SYS_ERR_EINVAL;

    process_t *curr = process_get_current();
    if (!curr) return SYS_ERR_EBADF;

    uint64_t old_handler = curr->signal_handlers[sig];
    curr->signal_handlers[sig] = handler_uptr;
    return (int64_t)old_handler;
}

static int64_t sys_handle_getuid(void) {
    process_t *curr = process_get_current();
    return curr ? curr->uid : 0;
}

static int64_t sys_handle_setuid(uint32_t uid) {
    process_t *curr = process_get_current();
    if (!curr) return SYS_ERR_EBADF;

    if (curr->uid != 0 && curr->uid != uid) {
        return SYS_ERR_EPERM;
    }
    curr->uid = uid;
    return 0;
}

static int64_t sys_handle_getgid(void) {
    process_t *curr = process_get_current();
    return curr ? curr->gid : 0;
}

static int64_t sys_handle_setgid(uint32_t gid) {
    process_t *curr = process_get_current();
    if (!curr) return SYS_ERR_EBADF;

    if (curr->uid != 0 && curr->gid != gid) {
        return SYS_ERR_EPERM;
    }
    curr->gid = gid;
    return 0;
}

static int64_t sys_handle_chmod(uint64_t path_uptr, uint32_t mode) {
    char kpath[256];
    if (copy_string_from_user(kpath, (const char *)path_uptr, sizeof(kpath)) < 0) {
        return SYS_ERR_EFAULT;
    }
    return vfs_chmod(kpath, mode);
}

static int64_t sys_handle_chown(uint64_t path_uptr, uint32_t uid, uint32_t gid) {
    char kpath[256];
    if (copy_string_from_user(kpath, (const char *)path_uptr, sizeof(kpath)) < 0) {
        return SYS_ERR_EFAULT;
    }
    return vfs_chown(kpath, uid, gid);
}

static int64_t sys_handle_shm_get(int key, size_t size, int flags) {
    return shm_get(key, size, flags);
}

static int64_t sys_handle_shm_at(int id, uint64_t addr, int flags) {
    return (int64_t)shm_at(id, addr, flags);
}

static int64_t sys_handle_shm_dt(uint64_t addr) {
    return shm_dt(addr);
}

static int64_t sys_handle_shm_ctl(int id, int cmd, uint64_t buf_uptr) {
    return shm_ctl(id, cmd, (void *)buf_uptr);
}

static int64_t sys_handle_getrandom(uint64_t buf_uptr, size_t len) {
    if (!user_validate_pointer((void *)buf_uptr, len, true)) {
        return SYS_ERR_EFAULT;
    }
    if (len > 4096) len = 4096;
    return kernel_getrandom((void *)buf_uptr, len, 0);
}

static int64_t sys_handle_spawn2(uint64_t path_uptr, uint64_t argv_uptr, int in_fd, int out_fd) {
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

    process_t *child = process_spawn_elf_redirect(kpath, kargv, in_fd, out_fd);
    if (!child) return SYS_ERR_ENOENT;
    return (int64_t)child->pid;
}

static int64_t sys_handle_secinfo(uint64_t info_uptr) {
    if (!user_validate_pointer((void *)info_uptr, sizeof(secinfo_t), true)) {
        return SYS_ERR_EFAULT;
    }
    return kernel_secinfo((secinfo_t *)info_uptr);
}

static int64_t sys_handle_getprocs(uint64_t uptr, size_t max_count) {
    if (max_count > 64) max_count = 64;
    size_t bytes = max_count * sizeof(proc_info_t);
    if (!user_validate_pointer((void *)uptr, bytes, true)) {
        return SYS_ERR_EFAULT;
    }
    proc_info_t kbuf[64];
    int count = process_get_table(kbuf, max_count);
    if (copy_to_user((void *)uptr, kbuf, count * sizeof(proc_info_t)) < 0) {
        return SYS_ERR_EFAULT;
    }
    return count;
}

static int64_t sys_handle_time(uint64_t tloc_uptr) {
    uint64_t sec = clock_get_epoch_seconds();
    if (tloc_uptr != 0) {
        if (!user_validate_pointer((void *)tloc_uptr, sizeof(uint64_t), true)) {
            return SYS_ERR_EFAULT;
        }
        if (copy_to_user((void *)tloc_uptr, &sec, sizeof(sec)) < 0) {
            return SYS_ERR_EFAULT;
        }
    }
    return (int64_t)sec;
}

static int64_t sys_handle_clock_gettime(int clk_id, uint64_t tp_uptr) {
    if (!user_validate_pointer((void *)tp_uptr, sizeof(struct timespec), true)) {
        return SYS_ERR_EFAULT;
    }
    struct timespec ts;
    int res = clock_gettime(clk_id, &ts);
    if (res != 0) return SYS_ERR_EINVAL;
    if (copy_to_user((void *)tp_uptr, &ts, sizeof(ts)) < 0) {
        return SYS_ERR_EFAULT;
    }
    return 0;
}

static int64_t sys_handle_nanosleep(uint64_t req_uptr, uint64_t rem_uptr) {
    if (!user_validate_pointer((void *)req_uptr, sizeof(struct timespec), false)) {
        return SYS_ERR_EFAULT;
    }
    struct timespec req;
    if (copy_from_user(&req, (void *)req_uptr, sizeof(req)) < 0) {
        return SYS_ERR_EFAULT;
    }
    uint64_t ms = (uint64_t)req.tv_sec * 1000 + (uint64_t)(req.tv_nsec / 1000000);
    if (ms == 0 && req.tv_nsec > 0) ms = 1;

    timer_sleep(ms);

    if (rem_uptr != 0) {
        if (user_validate_pointer((void *)rem_uptr, sizeof(struct timespec), true)) {
            struct timespec rem = {0, 0};
            copy_to_user((void *)rem_uptr, &rem, sizeof(rem));
        }
    }
    return 0;
}

static int64_t sys_handle_chdir(uint64_t path_uptr) {
    char kpath[256];
    if (copy_string_from_user(kpath, (const char *)path_uptr, sizeof(kpath)) < 0) {
        return SYS_ERR_EFAULT;
    }
    return vfs_chdir(kpath);
}

static int64_t sys_handle_getcwd(uint64_t buf_uptr, size_t size) {
    if (!user_validate_pointer((void *)buf_uptr, size, true)) {
        return SYS_ERR_EFAULT;
    }
    char kbuf[256];
    int res = vfs_getcwd(kbuf, sizeof(kbuf));
    if (res != 0) return res;
    size_t len = strlen(kbuf) + 1;
    if (len > size) return SYS_ERR_ERANGE;
    if (copy_to_user((void *)buf_uptr, kbuf, len) < 0) {
        return SYS_ERR_EFAULT;
    }
    return (int64_t)len;
}

static int64_t sys_handle_sync(void) {
    return vfs_sync();
}

static int64_t sys_handle_reboot(int cmd) {
    process_t *curr = process_get_current();
    if (!curr || curr->uid != 0) {
        return SYS_ERR_EPERM;
    }

    klog_write(KLOG_LEVEL_INFO, "[KERNEL] System shutdown/reboot requested");
    vfs_sync();

    if (cmd == REBOOT_CMD_REBOOT) {
        uint8_t good = 0x02;
        while (good & 0x02) {
            good = inb(0x64);
        }
        outb(0x64, 0xFE);
        outb(0xCF9, 0x06);
    } else {
        /* Poweroff */
        outw(0x604, 0x2000);
        outw(0xB004, 0x2000);
        outw(0x4004, 0x3400);
    }

    while (1) {
        __asm__ volatile ("cli; hlt");
    }
    return 0;
}

static int64_t sys_handle_klog(int action, uint64_t buf_uptr, size_t len) {
    if (action == 1) {
        char kmsg[128];
        if (copy_string_from_user(kmsg, (const char *)buf_uptr, sizeof(kmsg)) < 0) {
            return SYS_ERR_EFAULT;
        }
        klog_write(KLOG_LEVEL_INFO, kmsg);
        return 0;
    } else if (action == 2) {
        if (!user_validate_pointer((void *)buf_uptr, len, true)) {
            return SYS_ERR_EFAULT;
        }
        char kbuf[1024];
        if (len > sizeof(kbuf)) len = sizeof(kbuf);
        int bytes = klog_read_entries(kbuf, len, true);
        if (bytes > 0) {
            if (copy_to_user((void *)buf_uptr, kbuf, bytes) < 0) {
                return SYS_ERR_EFAULT;
            }
        }
        return bytes;
    }
    return SYS_ERR_EINVAL;
}

static int64_t sys_handle_sysinfo(uint64_t info_uptr) {
    if (!user_validate_pointer((void *)info_uptr, sizeof(sysinfo_data_t), true)) {
        return SYS_ERR_EFAULT;
    }
    sysinfo_data_t info;
    memset(&info, 0, sizeof(info));
    info.uptime_sec = timer_uptime_sec();
    info.total_ram = pmm_usable_memory();
    info.used_ram = pmm_used_memory();
    info.free_ram = pmm_free_memory();
    info.process_count = (uint32_t)process_count();
    memcpy(info.kernel_ver, "0.9.0", 5);
    memcpy(info.machine, "x86_64", 6);

    if (copy_to_user((void *)info_uptr, &info, sizeof(info)) < 0) {
        return SYS_ERR_EFAULT;
    }
    return 0;
}

static int64_t sys_handle_tty_ctrl(int cmd, uint64_t arg) {
    tty_t *tty = tty_get_current();
    if (!tty) return SYS_ERR_ENODEV;

    if (cmd == TTY_CTRL_GET_PGRP) {
        return (int64_t)tty_get_foreground_pgrp(tty);
    } else if (cmd == TTY_CTRL_SET_PGRP) {
        tty_set_foreground_pgrp(tty, (uint32_t)arg);
        return 0;
    }
    return SYS_ERR_EINVAL;
}

static int64_t sys_handle_execve(uint64_t path_uptr, uint64_t argv_uptr, uint64_t envp_uptr) {
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

    char *kenvp[32] = {0};
    char env_bufs[32][128];
    if (envp_uptr != 0) {
        const char **uenvp = (const char **)envp_uptr;
        for (int i = 0; i < 31; i++) {
            if (!user_validate_pointer(&uenvp[i], sizeof(char *), false)) break;
            const char *uenv = uenvp[i];
            if (!uenv) break;
            if (copy_string_from_user(env_bufs[i], uenv, sizeof(env_bufs[i])) < 0) break;
            kenvp[i] = env_bufs[i];
        }
    }

    process_t *curr = process_get_current();
    if (!curr) return SYS_ERR_EBADF;

    int ret = process_execve(curr, kpath, kargv, kenvp);
    return (ret == NYOTA_OK) ? 0 : SYS_ERR_ENOENT;
}

/* ── Syscall Dispatcher ───────────────────────────────────────────────────── */

int64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    switch (num) {
        case SYS_WRITE:     return sys_handle_write(a1, a2, a3);
        case SYS_EXIT:      return sys_handle_exit((int)a1);
        case SYS_GETPID:    return sys_handle_getpid();
        case SYS_YIELD:     return sys_handle_yield();
        case SYS_SLEEP:     return sys_handle_sleep(a1);
        case SYS_OPEN:      return sys_handle_open(a1, a2, a3);
        case SYS_CLOSE:     return sys_handle_close(a1);
        case SYS_READ:      return sys_handle_read(a1, a2, a3);
        case SYS_SEEK:      return sys_handle_seek(a1, a2, a3);
        case SYS_STAT:      return sys_handle_stat(a1, a2);
        case SYS_GETDENTS:  return sys_handle_getdents(a1, a2, a3);
        case SYS_MKDIR:     return sys_handle_mkdir(a1, a2);
        case SYS_CREATE:    return sys_handle_create(a1, a2);
        case SYS_EXEC:      return sys_handle_exec(a1, a2);
        case SYS_SPAWN:     return sys_handle_spawn(a1, a2);
        case SYS_WAITPID:   return sys_handle_waitpid(a1, a2, a3);
        case SYS_SOCKET:    return sys_handle_socket((int)a1, (int)a2, (int)a3);
        case SYS_BIND:      return sys_handle_bind((int)a1, a2, (size_t)a3);
        case SYS_LISTEN:    return sys_handle_listen((int)a1, (int)a2);
        case SYS_ACCEPT:    return sys_handle_accept((int)a1, a2, a3);
        case SYS_CONNECT:   return sys_handle_connect((int)a1, a2, (size_t)a3);
        case SYS_SEND:      return sys_handle_send((int)a1, a2, (size_t)a3, (int)a4);
        case SYS_RECV:      return sys_handle_recv((int)a1, a2, (size_t)a3, (int)a4);
        case SYS_SENDTO:    return sys_handle_sendto((int)a1, a2, (size_t)a3, (int)a4, a5, (size_t)a6);
        case SYS_RECVFROM:  return sys_handle_recvfrom((int)a1, a2, (size_t)a3, (int)a4, a5, a6);
        case SYS_SHUTDOWN:  return sys_handle_shutdown((int)a1, (int)a2);
        case SYS_PIPE:      return sys_handle_pipe(a1);
        case SYS_DUP2:      return sys_handle_dup2((int)a1, (int)a2);
        case SYS_KILL:      return sys_handle_kill((int)a1, (int)a2);
        case SYS_SIGNAL:    return sys_handle_signal((int)a1, a2);
        case SYS_GETUID:    return sys_handle_getuid();
        case SYS_SETUID:    return sys_handle_setuid((uint32_t)a1);
        case SYS_GETGID:    return sys_handle_getgid();
        case SYS_SETGID:    return sys_handle_setgid((uint32_t)a1);
        case SYS_CHMOD:     return sys_handle_chmod(a1, (uint32_t)a2);
        case SYS_CHOWN:     return sys_handle_chown(a1, (uint32_t)a2, (uint32_t)a3);
        case SYS_SHM_GET:   return sys_handle_shm_get((int)a1, (size_t)a2, (int)a3);
        case SYS_SHM_AT:    return sys_handle_shm_at((int)a1, a2, (int)a3);
        case SYS_SHM_DT:    return sys_handle_shm_dt(a1);
        case SYS_SHM_CTL:   return sys_handle_shm_ctl((int)a1, (int)a2, a3);
        case SYS_GETRANDOM: return sys_handle_getrandom(a1, (size_t)a2);
        case SYS_SPAWN2:    return sys_handle_spawn2(a1, a2, (int)a3, (int)a4);
        case SYS_SECINFO:   return sys_handle_secinfo(a1);
        case SYS_GETPROCS:  return sys_handle_getprocs(a1, (size_t)a2);
        case SYS_TIME:          return sys_handle_time(a1);
        case SYS_CLOCK_GETTIME: return sys_handle_clock_gettime((int)a1, a2);
        case SYS_NANOSLEEP:     return sys_handle_nanosleep(a1, a2);
        case SYS_CHDIR:         return sys_handle_chdir(a1);
        case SYS_GETCWD:        return sys_handle_getcwd(a1, (size_t)a2);
        case SYS_SYNC:          return sys_handle_sync();
        case SYS_REBOOT:        return sys_handle_reboot((int)a1);
        case SYS_KLOG:          return sys_handle_klog((int)a1, a2, (size_t)a3);
        case SYS_SYSINFO:       return sys_handle_sysinfo(a1);
        case SYS_TTY_CTRL:      return sys_handle_tty_ctrl((int)a1, a2);
        case SYS_EXECVE:        return sys_handle_execve(a1, a2, a3);
        default:            return SYS_ERR_ENOSYS;
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

    /* Check and deliver any pending signals before returning to Ring 3 */
    signal_check_and_deliver(frame);
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

int64_t sys_waitpid(int32_t pid, int *status) {
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

int64_t sys_pipe(int fds[2]) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov $26, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"(fds)
        : "rax", "rdi", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_dup2(int oldfd, int newfd) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov $27, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"((uint64_t)oldfd), "r"((uint64_t)newfd)
        : "rax", "rdi", "rsi", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_kill(int32_t pid, int sig) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov $28, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"((uint64_t)pid), "r"((uint64_t)sig)
        : "rax", "rdi", "rsi", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_signal(int sig, void *handler) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov $29, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"((uint64_t)sig), "r"(handler)
        : "rax", "rdi", "rsi", "rcx", "r11", "memory"
    );
    return ret;
}

uint32_t sys_getuid(void) {
    uint64_t ret;
    __asm__ volatile (
        "mov $30, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        :
        : "rax", "rcx", "r11", "memory"
    );
    return (uint32_t)ret;
}

int64_t sys_setuid(uint32_t uid) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov $31, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"((uint64_t)uid)
        : "rax", "rdi", "rcx", "r11", "memory"
    );
    return ret;
}

uint32_t sys_getgid(void) {
    uint64_t ret;
    __asm__ volatile (
        "mov $32, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        :
        : "rax", "rcx", "r11", "memory"
    );
    return (uint32_t)ret;
}

int64_t sys_setgid(uint32_t gid) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov $33, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"((uint64_t)gid)
        : "rax", "rdi", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_chmod(const char *path, uint32_t mode) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov $34, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"(path), "r"((uint64_t)mode)
        : "rax", "rdi", "rsi", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_chown(const char *path, uint32_t uid, uint32_t gid) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov %3, %%rdx\n"
        "mov $35, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"(path), "r"((uint64_t)uid), "r"((uint64_t)gid)
        : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_shm_get(uint32_t key, size_t size, int flags) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov %3, %%rdx\n"
        "mov $36, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"((uint64_t)key), "r"(size), "r"((uint64_t)flags)
        : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory"
    );
    return ret;
}

void *sys_shm_at(int shmid, const void *addr, int flags) {
    uint64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov %3, %%rdx\n"
        "mov $37, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"((uint64_t)shmid), "r"(addr), "r"((uint64_t)flags)
        : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory"
    );
    return (void *)ret;
}

int64_t sys_shm_dt(const void *addr) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov $38, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"(addr)
        : "rax", "rdi", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_shm_ctl(int shmid, int cmd, void *buf) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov %3, %%rdx\n"
        "mov $39, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"((uint64_t)shmid), "r"((uint64_t)cmd), "r"(buf)
        : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_getrandom(void *buf, size_t len, unsigned int flags) {
    (void)flags;
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov $40, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"(buf), "r"(len)
        : "rax", "rdi", "rsi", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_spawn2(const char *path, char *const argv[], int in_fd, int out_fd) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov %3, %%rdx\n"
        "mov %4, %%r10\n"
        "mov $41, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"(path), "r"(argv), "r"((uint64_t)in_fd), "r"((uint64_t)out_fd)
        : "rax", "rdi", "rsi", "rdx", "r10", "rcx", "r11", "memory"
    );
    return ret;
}

int64_t sys_secinfo(void *info) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov $42, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"(info)
        : "rax", "rdi", "rcx", "r11", "memory"
    );
    return ret;
}
