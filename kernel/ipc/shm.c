/* =============================================================================
 * Nyota OS — Controlled Shared Memory IPC Implementation (Phase 8)
 * Page-aligned, permission-checked, and reference-counted shared memory.
 * =========================================================================== */

#include "ipc/shm.h"
#include "pmm.h"
#include "paging.h"
#include "memory.h"
#include "process.h"
#include "security/capability.h"
#include "syscall.h"

static shm_segment_t shm_table[MAX_SHM_SEGMENTS];
static uint32_t active_shm_count = 0;

void shm_init(void) {
    memset(shm_table, 0, sizeof(shm_table));
    active_shm_count = 0;
}

uint32_t shm_active_count(void) {
    return active_shm_count;
}

static bool check_shm_permission(process_t *proc, shm_segment_t *seg, bool write) {
    if (!proc || !seg) return false;
    if (proc->uid == 0 || has_capability(proc, CAP_IPC_OWNER)) return true;

    if (proc->uid == seg->owner_uid) {
        return write ? ((seg->mode & 0200) != 0) : ((seg->mode & 0400) != 0);
    } else if (proc->gid == seg->owner_gid) {
        return write ? ((seg->mode & 0020) != 0) : ((seg->mode & 0040) != 0);
    } else {
        return write ? ((seg->mode & 0002) != 0) : ((seg->mode & 0004) != 0);
    }
}

int64_t shm_get(uint32_t key, size_t size, int flags) {
    process_t *curr = process_get_current();
    if (!curr) return -SYS_ERR_EBADF;

    if (size == 0 || size > MAX_SHM_PAGES * PAGE_SIZE) {
        return -SYS_ERR_EINVAL;
    }

    /* Check if key already exists */
    for (int i = 0; i < MAX_SHM_SEGMENTS; i++) {
        if (shm_table[i].in_use && shm_table[i].key == key && key != 0) {
            if ((flags & IPC_CREAT) && (flags & IPC_EXCL)) {
                return SYS_ERR_EEXIST;
            }
            if (!check_shm_permission(curr, &shm_table[i], false)) {
                return SYS_ERR_EACCES;
            }
            return shm_table[i].shmid;
        }
    }

    if (!(flags & IPC_CREAT)) {
        return SYS_ERR_ENOENT;
    }

    /* Allocate new segment */
    int slot = -1;
    for (int i = 0; i < MAX_SHM_SEGMENTS; i++) {
        if (!shm_table[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return SYS_ERR_ENOSPC;

    size_t page_count = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    shm_segment_t *seg = &shm_table[slot];
    memset(seg, 0, sizeof(shm_segment_t));

    for (size_t p = 0; p < page_count; p++) {
        void *page = pmm_alloc_page();
        if (!page) {
            for (size_t k = 0; k < p; k++) {
                pmm_free_page(seg->phys_pages[k]);
            }
            return SYS_ERR_ENOMEM;
        }
        memset(page, 0, PAGE_SIZE);
        seg->phys_pages[p] = page;
    }

    seg->shmid = slot + 1;
    seg->key = key;
    seg->size = size;
    seg->page_count = page_count;
    seg->owner_uid = curr->uid;
    seg->owner_gid = curr->gid;
    seg->mode = (flags & 0777) ? (flags & 0777) : 0660;
    seg->in_use = true;
    active_shm_count++;

    return seg->shmid;
}

void *shm_at(int shmid, uint64_t addr, int flags) {
    (void)addr;
    process_t *curr = process_get_current();
    if (!curr) return (void *)(int64_t)SYS_ERR_EBADF;

    if (shmid < 1 || shmid > MAX_SHM_SEGMENTS) {
        return (void *)(int64_t)SYS_ERR_EINVAL;
    }

    shm_segment_t *seg = &shm_table[shmid - 1];
    if (!seg->in_use) return (void *)(int64_t)SYS_ERR_EINVAL;

    bool write = !(flags & 010000); /* Check read-only flag */
    if (!check_shm_permission(curr, seg, write)) {
        return (void *)(int64_t)SYS_ERR_EACCES;
    }

    if (curr->shm_count >= 8) {
        return (void *)(int64_t)SYS_ERR_ENOSPC;
    }

    /* Compute page-aligned user address */
    uint64_t vaddr = SHM_USER_VIRT_BASE + ((uint64_t)(shmid - 1) * (MAX_SHM_PAGES * PAGE_SIZE));
    page_table_t *pml4 = (page_table_t *)curr->cr3;

    for (size_t p = 0; p < seg->page_count; p++) {
        uint64_t page_vaddr = vaddr + (p * PAGE_SIZE);
        paging_map_page_in(pml4, page_vaddr, (uint64_t)seg->phys_pages[p],
                           PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE);
        paging_invlpg(page_vaddr);
    }

    curr->shm_ids[curr->shm_count] = shmid;
    curr->shm_addrs[curr->shm_count] = vaddr;
    curr->shm_count++;
    seg->attach_count++;

    return (void *)vaddr;
}

int64_t shm_dt(uint64_t addr) {
    process_t *curr = process_get_current();
    if (!curr) return SYS_ERR_EBADF;

    uint64_t target = addr;
    int index = -1;

    for (uint32_t i = 0; i < curr->shm_count; i++) {
        if (curr->shm_addrs[i] == target) {
            index = (int)i;
            break;
        }
    }
    if (index < 0) return SYS_ERR_EINVAL;

    int shmid = curr->shm_ids[index];
    shm_segment_t *seg = &shm_table[shmid - 1];
    page_table_t *pml4 = (page_table_t *)curr->cr3;

    for (size_t p = 0; p < seg->page_count; p++) {
        uint64_t page_vaddr = target + (p * PAGE_SIZE);
        paging_unmap_page_in(pml4, page_vaddr);
        paging_invlpg(page_vaddr);
    }

    /* Remove from process table */
    for (uint32_t i = index; i < curr->shm_count - 1; i++) {
        curr->shm_ids[i] = curr->shm_ids[i + 1];
        curr->shm_addrs[i] = curr->shm_addrs[i + 1];
    }
    curr->shm_count--;

    if (seg->attach_count > 0) seg->attach_count--;

    if (seg->attach_count == 0 && seg->marked_for_deletion) {
        for (size_t p = 0; p < seg->page_count; p++) {
            pmm_free_page(seg->phys_pages[p]);
        }
        seg->in_use = false;
        if (active_shm_count > 0) active_shm_count--;
    }

    return 0;
}

int64_t shm_ctl(int shmid, int cmd, void *buf) {
    (void)buf;
    process_t *curr = process_get_current();
    if (!curr) return SYS_ERR_EBADF;

    if (shmid < 1 || shmid > MAX_SHM_SEGMENTS) return SYS_ERR_EINVAL;

    shm_segment_t *seg = &shm_table[shmid - 1];
    if (!seg->in_use) return SYS_ERR_EINVAL;

    if (curr->uid != 0 && curr->uid != seg->owner_uid && !has_capability(curr, CAP_IPC_OWNER)) {
        return SYS_ERR_EPERM;
    }

    if (cmd == IPC_RMID) {
        seg->marked_for_deletion = true;
        if (seg->attach_count == 0) {
            for (size_t p = 0; p < seg->page_count; p++) {
                pmm_free_page(seg->phys_pages[p]);
            }
            seg->in_use = false;
            if (active_shm_count > 0) active_shm_count--;
        }
        return 0;
    }

    return SYS_ERR_EINVAL;
}

void shm_process_init(process_t *proc) {
    if (!proc) return;
    proc->shm_count = 0;
    for (int i = 0; i < 8; i++) {
        proc->shm_ids[i] = 0;
        proc->shm_addrs[i] = 0;
    }
}

void shm_process_cleanup(process_t *proc) {
    if (!proc) return;
    while (proc->shm_count > 0) {
        shm_dt(proc->shm_addrs[0]);
    }
}

void shm_cleanup_process(process_t *proc) {
    shm_process_cleanup(proc);
}
