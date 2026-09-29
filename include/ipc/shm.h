/* =============================================================================
 * Nyota OS — Controlled Shared Memory IPC Interface (Phase 8)
 * Page-aligned, permission-checked, and reference-counted shared memory.
 * =========================================================================== */

#ifndef NYOTA_IPC_SHM_H
#define NYOTA_IPC_SHM_H

#include "types.h"

#define MAX_SHM_SEGMENTS        16
#define MAX_SHM_PAGES           16
#define SHM_USER_VIRT_BASE      0x0000008000180000ULL

#define IPC_CREAT               01000
#define IPC_EXCL                02000
#define IPC_RMID                0
#define IPC_STAT                1

struct process;

typedef struct shm_segment {
    int      shmid;
    uint32_t key;
    size_t   size;
    size_t   page_count;
    void    *phys_pages[MAX_SHM_PAGES];

    uint32_t owner_uid;
    uint32_t owner_gid;
    uint32_t mode;

    uint32_t attach_count;
    bool     marked_for_deletion;
    bool     in_use;
} shm_segment_t;

void shm_init(void);

int64_t shm_get(uint32_t key, size_t size, int flags);
void *shm_at(int shmid, uint64_t addr, int flags);
int64_t shm_dt(uint64_t addr);
int64_t shm_ctl(int shmid, int cmd, void *buf);

void shm_process_init(struct process *proc);
void shm_process_cleanup(struct process *proc);
uint32_t shm_active_count(void);

#endif /* NYOTA_IPC_SHM_H */
