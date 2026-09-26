/* =============================================================================
 * Nyota OS — System Call Interface & ABI (Phase 5)
 * Vector 0x80 syscalls with System V register ABI and hostile pointer validation.
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

/* POSIX Error Codes */
#define SYS_ERR_NONE        0
#define SYS_ERR_EPERM       (-1)   /* Operation not permitted */
#define SYS_ERR_ENOENT      (-2)   /* No such file or directory */
#define SYS_ERR_EFAULT      (-14)  /* Bad address / pointer */
#define SYS_ERR_EINVAL      (-22)  /* Invalid argument */
#define SYS_ERR_ENOSYS      (-38)  /* Function not implemented */

/* Syscall Dispatcher APIs */
void syscall_init(void);
void syscall_handler(interrupt_frame_t *frame);
int64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);

/* User Memory Validation & Safe Copy */
bool user_validate_pointer(const void *uptr, size_t len, bool write);
int copy_from_user(void *kdest, const void *usrc, size_t max_len);
int copy_to_user(void *udest, const void *ksrc, size_t len);

/* User-mode Library Wrappers */
int64_t sys_write(const char *buf, size_t len);
void sys_exit(int status) __attribute__((noreturn));
int32_t sys_getpid(void);
void sys_yield(void);
int64_t sys_sleep(uint64_t ms);

#endif /* NYOTA_SYSCALL_H */
