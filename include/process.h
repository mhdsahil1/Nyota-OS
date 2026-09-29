/* =============================================================================
 * Nyota OS — Process Structure & User Task Abstraction (Phase 6)
 * Process Control Block (PCB), PID management, file descriptors, and ELF execution.
 * =========================================================================== */

#ifndef NYOTA_PROCESS_H
#define NYOTA_PROCESS_H

#include "types.h"
#include "interrupts.h"
#include "fs/vfs.h"

/* User Address Space Layout (Located in PML4[1] for total hardware isolation) */
#define USER_SPACE_BASE         0x0000008000000000ULL  /* 512 GB mark */
#define USER_SPACE_END          0x0000008000200000ULL  /* 2 MB region */
#define USER_CODE_BASE          0x0000008000000000ULL
#define USER_STACK_TOP          0x0000008000104000ULL  /* 16 KiB user stack */
#define USER_STACK_SIZE         (16 * 1024ULL)

#define PROCESS_NAME_MAX        32
#define PROCESS_MAX_COUNT       32

/* Process States (Phase 5/6/8 Lifecycle) */
typedef enum {
    PROCESS_NEW = 0,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_SLEEPING,
    PROCESS_ZOMBIE,
    PROCESS_TERMINATED,
    PROCESS_IDLE
} process_state_t;

/* Process Control Block (PCB) */
typedef struct process {
    uint32_t pid;
    uint32_t parent_pid;
    char name[PROCESS_NAME_MAX];
    process_state_t state;
    int exit_status;

    uint32_t uid;
    uint32_t gid;
    uint64_t capabilities;

    /* Resource limits */
    uint64_t max_memory;
    uint32_t max_open_files;
    uint32_t max_processes;
    uint32_t max_sockets;

    /* Signals */
    uint32_t pending_signals;
    uint32_t blocked_signals;
    uint64_t signal_handlers[32];

    uint64_t entry_point;
    uint64_t user_stack_top;
    uint64_t kernel_stack;       /* Allocated kernel stack base */
    uint64_t kernel_stack_top;   /* Top of kernel stack (TSS.RSP0) */
    uint64_t saved_rsp;          /* Saved stack pointer pointing to interrupt_frame_t */

    uint64_t cr3;                /* Process PML4 physical address */

    file_t *fds[MAX_PROCESS_FDS];/* Per-process file descriptor table */

    /* Shared memory attachments (up to 8) */
    int shm_ids[8];
    uint64_t shm_addrs[8];
    uint32_t shm_count;

    uint64_t wakeup_tick;        /* Absolute timer tick to wake from SLEEPING */
    uint64_t runtime_ticks;      /* Total PIT ticks spent in RUNNING state */
    uint64_t context_switches;   /* Number of times scheduled */

    char cwd[128];               /* Current working directory */
    uint32_t pgrp;               /* Process group ID (for job control) */
    uint64_t start_time;         /* Process start time in seconds or ticks */

    /* Process hierarchy tree */
    struct process *parent;
    struct process *children;
    struct process *next_sibling;

    /* Waiter sleeping in waitpid */
    struct process *wait_parent;

    struct process *next;        /* Queue link (Ready / Sleep / Process list) */
    struct process *prev;
} process_t;

/* Process Subsystem APIs */
void process_system_init(void);

process_t *process_create(const char *name, uint64_t entry_point, const void *code_blob, size_t code_size);
process_t *process_create_from_elf(const char *path, char *const argv[]);
process_t *process_spawn_elf(const char *path, char *const argv[]);
process_t *process_spawn_elf_redirect(const char *path, char *const argv[], int in_fd, int out_fd);
int process_exec(process_t *proc, const char *path, char *const argv[]);
int process_execve(process_t *proc, const char *path, char *const argv[], char *const envp[]);
#define WNOHANG         1

int process_waitpid(int32_t pid, int *status, int options);

typedef struct {
    uint32_t pid;
    uint32_t ppid;
    uint32_t uid;
    char state[16];
    char name[32];
} proc_info_t;

process_t *process_find(uint32_t pid);
size_t process_count(void);
void process_list(void);
int process_get_table(proc_info_t *out, size_t max_count);

process_t *process_get_current(void);
void process_set_current(process_t *proc);
void process_exit(int status);

/* Helper to spawn/run initial or specific process */
process_t *process_spawn_init(void);
void process_run(process_t *proc);

#endif /* NYOTA_PROCESS_H */
