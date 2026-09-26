/* =============================================================================
 * Nyota OS — Process Structure & User Task Abstraction
 * Process Control Block (PCB), PID allocation, memory layout, and execution.
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

/* Process States */
typedef enum {
    PROCESS_READY = 0,
    PROCESS_RUNNING,
    PROCESS_TERMINATED
} process_state_t;

/* Process Control Block (PCB) */
typedef struct process {
    uint32_t pid;
    process_state_t state;
    int exit_status;

    uint64_t entry_point;
    uint64_t user_stack_top;
    uint64_t kernel_stack_top;

    uint64_t cr3;           /* Process Page Directory (PML4) physical address */
    struct process *next;
} process_t;

/* Process Subsystem APIs */
void process_system_init(void);

process_t *process_create(uint64_t entry_point, const void *code_blob, size_t code_size);
process_t *process_spawn_init(void);
process_t *process_get_current(void);
void process_run(process_t *proc) __attribute__((noreturn));
void process_exit(int status) __attribute__((noreturn));

/* User mode transition assembly routine */
void user_enter_ring3(uint64_t entry_point, uint64_t user_rsp) __attribute__((noreturn));

#endif /* NYOTA_PROCESS_H */
