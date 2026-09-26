/* =============================================================================
 * Nyota OS — Process Structure & User Task Abstraction (Phase 5)
 * Process Control Block (PCB), PID management, scheduling metrics, and states.
 * =========================================================================== */

#ifndef NYOTA_PROCESS_H
#define NYOTA_PROCESS_H

#include "types.h"
#include "interrupts.h"

/* User Address Space Layout (Located in PML4[1] for total hardware isolation) */
#define USER_SPACE_BASE         0x0000008000000000ULL  /* 512 GB mark */
#define USER_SPACE_END          0x0000008000200000ULL  /* 2 MB region */
#define USER_CODE_BASE          0x0000008000000000ULL
#define USER_STACK_TOP          0x0000008000104000ULL  /* 16 KiB user stack */
#define USER_STACK_SIZE         (16 * 1024ULL)

#define PROCESS_NAME_MAX        32
#define PROCESS_MAX_COUNT       32

/* Process States (Phase 5 Lifecycle) */
typedef enum {
    PROCESS_NEW = 0,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_SLEEPING,
    PROCESS_TERMINATED,
    PROCESS_IDLE
} process_state_t;

/* Process Control Block (PCB) */
typedef struct process {
    uint32_t pid;
    char name[PROCESS_NAME_MAX];
    process_state_t state;
    int exit_status;

    uint64_t entry_point;
    uint64_t user_stack_top;
    uint64_t kernel_stack;       /* Allocated kernel stack base */
    uint64_t kernel_stack_top;   /* Top of kernel stack (TSS.RSP0) */
    uint64_t saved_rsp;          /* Saved stack pointer pointing to interrupt_frame_t */

    uint64_t cr3;                /* Process PML4 physical address */

    uint64_t wakeup_tick;        /* Absolute timer tick to wake from SLEEPING */
    uint64_t runtime_ticks;      /* Total PIT ticks spent in RUNNING state */
    uint64_t context_switches;   /* Number of times scheduled */

    struct process *next;        /* Queue link (Ready / Sleep / Process list) */
    struct process *prev;
} process_t;

/* Process Subsystem APIs */
void process_system_init(void);

process_t *process_create(const char *name, uint64_t entry_point, const void *code_blob, size_t code_size);
process_t *process_find(uint32_t pid);
size_t process_count(void);
void process_list(void);

process_t *process_get_current(void);
void process_set_current(process_t *proc);
void process_exit(int status);

/* Helper to spawn/run initial or specific process */
process_t *process_spawn_init(void);
void process_run(process_t *proc);

#endif /* NYOTA_PROCESS_H */
