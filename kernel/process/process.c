/* =============================================================================
 * Nyota OS — Process Subsystem & Process Control Block (PCB) Management (Phase 6)
 * Process table, PID allocation, memory isolation, ELF execution, and descriptors.
 * =========================================================================== */

#include "process.h"
#include "scheduler.h"
#include "paging.h"
#include "pmm.h"
#include "heap.h"
#include "tss.h"
#include "console.h"
#include "vga.h"
#include "serial.h"
#include "memory.h"
#include "kernel.h"
#include "gdt.h"
#include "user_programs.h"
#include "fs/vfs.h"
#include "elf/elf.h"
#include "signal.h"
#include "security/random.h"
#include "security/capability.h"
#include "ipc/shm.h"
#include "timer.h"
#include "syscall.h"

static process_t *process_table[PROCESS_MAX_COUNT] = {0};
static process_t process_table_storage[PROCESS_MAX_COUNT];
static uint8_t process_kernel_stacks[PROCESS_MAX_COUNT][16384] __attribute__((aligned(16)));
static bool process_slot_in_use[PROCESS_MAX_COUNT] = {false};
static uint32_t next_pid = 1;

/* ── Process Hierarchy Management ─────────────────────────────────────────── */

void process_add_child(process_t *parent, process_t *child) {
    if (!parent || !child) return;
    child->parent = parent;
    child->parent_pid = parent->pid;
    child->next_sibling = parent->children;
    parent->children = child;
}

void process_remove_child(process_t *parent, process_t *child) {
    if (!parent || !child) return;
    if (parent->children == child) {
        parent->children = child->next_sibling;
        child->next_sibling = NULL;
        return;
    }
    process_t *prev = parent->children;
    while (prev && prev->next_sibling != child) {
        prev = prev->next_sibling;
    }
    if (prev) {
        prev->next_sibling = child->next_sibling;
        child->next_sibling = NULL;
    }
}


/* ── Process Management ───────────────────────────────────────────────────── */

void process_system_init(void) {
    for (size_t i = 0; i < PROCESS_MAX_COUNT; i++) {
        process_table[i] = NULL;
        process_slot_in_use[i] = false;
    }
    next_pid = 1;
}

process_t *process_get_current(void) {
    return scheduler_get_current();
}

void process_set_current(process_t *proc) {
    (void)proc;
}

process_t *process_find(uint32_t pid) {
    for (size_t i = 0; i < PROCESS_MAX_COUNT; i++) {
        if (process_table[i] && process_table[i]->pid == pid) {
            return process_table[i];
        }
    }
    return NULL;
}

size_t process_count(void) {
    size_t count = 0;
    for (size_t i = 0; i < PROCESS_MAX_COUNT; i++) {
        if (process_table[i]) {
            count++;
        }
    }
    return count;
}

/* ── Stack Setup Helper for argc / argv ───────────────────────────────────── */

static uint64_t setup_user_stack(page_table_t *pml4, uint64_t stack_top, const char *path, char *const argv[], char *const envp[], int *out_argc, uint64_t *out_user_argv, uint64_t *out_user_envp) {
    /* Determine top physical frame */
    uint64_t top_page_vaddr = stack_top - PAGE_SIZE;
    uint64_t top_page_phys = paging_get_physical_in(pml4, top_page_vaddr);
    if (top_page_phys == 0) return 0;

    /* Count argc */
    int argc = 0;
    if (argv) {
        while (argv[argc] != NULL && argc < 32) {
            argc++;
        }
    }
    if (argc == 0) {
        argc = 1;
    }

    /* Count envc */
    int envc = 0;
    static const char *default_envp[] = {
        "PATH=/bin:/sbin",
        "USER=sahil",
        "HOME=/home/sahil",
        "SHELL=/bin/sh",
        "TERM=nyota",
        NULL
    };
    char *const *active_envp = envp;
    if (!active_envp || !active_envp[0]) {
        active_envp = (char *const *)default_envp;
    }
    while (active_envp[envc] != NULL && envc < 32) {
        envc++;
    }

    /* Stack pointer starts at top of stack page */
    uint64_t user_rsp = stack_top;
    uint32_t offset = PAGE_SIZE;

    uint64_t arg_user_addrs[32];
    uint64_t env_user_addrs[32];

    /* Copy env strings to stack in reverse */
    for (int i = envc - 1; i >= 0; i--) {
        const char *estr = active_envp[i];
        size_t len = strlen(estr) + 1;
        if (offset < len) return 0;

        offset -= (uint32_t)len;
        user_rsp -= len;

        memcpy((void *)(top_page_phys + offset), estr, len);
        env_user_addrs[i] = user_rsp;
    }

    /* Copy arg strings to stack in reverse */
    for (int i = argc - 1; i >= 0; i--) {
        const char *arg_str = (argv && argv[i]) ? argv[i] : path;
        size_t len = strlen(arg_str) + 1;
        if (offset < len) return 0;

        offset -= (uint32_t)len;
        user_rsp -= len;

        memcpy((void *)(top_page_phys + offset), arg_str, len);
        arg_user_addrs[i] = user_rsp;
    }

    /* 8-byte align */
    uint64_t rem = user_rsp % 8;
    if (rem != 0) {
        user_rsp -= rem;
        offset -= (uint32_t)rem;
    }

    /* Push envp[envc] = NULL */
    user_rsp -= 8;
    offset -= 8;
    *(uint64_t *)(top_page_phys + offset) = 0;

    /* Push envp[i] pointers */
    for (int i = envc - 1; i >= 0; i--) {
        user_rsp -= 8;
        offset -= 8;
        *(uint64_t *)(top_page_phys + offset) = env_user_addrs[i];
    }
    uint64_t user_envp_ptr = user_rsp;

    /* Push argv[argc] = NULL */
    user_rsp -= 8;
    offset -= 8;
    *(uint64_t *)(top_page_phys + offset) = 0;

    /* Push argv[i] pointers */
    for (int i = argc - 1; i >= 0; i--) {
        user_rsp -= 8;
        offset -= 8;
        *(uint64_t *)(top_page_phys + offset) = arg_user_addrs[i];
    }
    uint64_t user_argv_ptr = user_rsp;

    /* 16-byte align for System V ABI */
    if (user_rsp % 16 != 0) {
        user_rsp -= 8;
        offset -= 8;
    }

    if (out_argc) *out_argc = argc;
    if (out_user_argv) *out_user_argv = user_argv_ptr;
    if (out_user_envp) *out_user_envp = user_envp_ptr;

    return user_rsp;
}

/* ── Create Process from Memory Blob (Phase 5 compatibility) ──────────────── */

process_t *process_create(const char *name, uint64_t entry_point, const void *code_blob, size_t code_size) {
    int slot = -1;
    for (size_t i = 0; i < PROCESS_MAX_COUNT; i++) {
        if (!process_slot_in_use[i]) {
            slot = (int)i;
            break;
        }
    }

    if (slot == -1) {
        kwarn("process_create: process table full");
        return NULL;
    }

    process_slot_in_use[slot] = true;
    process_t *proc = &process_table_storage[slot];
    memset(proc, 0, sizeof(process_t));

    proc->pid = next_pid++;
    proc->parent_pid = 0;
    proc->state = PROCESS_NEW;
    proc->exit_status = 0;
    proc->runtime_ticks = 0;
    proc->context_switches = 0;

    if (name) {
        size_t nlen = strlen(name);
        if (nlen >= PROCESS_NAME_MAX) nlen = PROCESS_NAME_MAX - 1;
        memcpy(proc->name, name, nlen);
        proc->name[nlen] = '\0';
    } else {
        memcpy(proc->name, "user_proc", 10);
    }

    /* Initialize file descriptors (stdin, stdout, stderr) */
    vfs_init_process_fds(proc->fds);

    /* Assign dedicated 16 KiB static kernel stack */
    proc->kernel_stack = (uint64_t)&process_kernel_stacks[slot][0];
    proc->kernel_stack_top = proc->kernel_stack + sizeof(process_kernel_stacks[slot]);

    /* Create address space */
    page_table_t *pml4 = paging_create_address_space();
    if (!pml4) {
        kwarn("process_create: failed to allocate process PML4");
        process_slot_in_use[slot] = false;
        return NULL;
    }
    proc->cr3 = (uint64_t)pml4;

    /* Map code page */
    void *code_phys = pmm_alloc_page();
    if (!code_phys) {
        process_slot_in_use[slot] = false;
        return NULL;
    }
    memset(code_phys, 0, PAGE_SIZE);

    if (code_blob && code_size > 0) {
        size_t copy_sz = (code_size > PAGE_SIZE) ? PAGE_SIZE : code_size;
        memcpy(code_phys, code_blob, copy_sz);
    }

    paging_map_page_in(pml4, USER_CODE_BASE, (uint64_t)code_phys, PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE);

    /* Map stack pages */
    uint64_t stack_base = USER_STACK_TOP - USER_STACK_SIZE;
    for (uint64_t sp = stack_base; sp < USER_STACK_TOP; sp += PAGE_SIZE) {
        void *stack_phys = pmm_alloc_page();
        if (!stack_phys) {
            process_slot_in_use[slot] = false;
            return NULL;
        }
        memset(stack_phys, 0, PAGE_SIZE);
        paging_map_page_in(pml4, sp, (uint64_t)stack_phys, PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE);
    }

    proc->entry_point = entry_point ? entry_point : USER_CODE_BASE;
    proc->user_stack_top = USER_STACK_TOP - 16;

    interrupt_frame_t *frame = (interrupt_frame_t *)(proc->kernel_stack_top - sizeof(interrupt_frame_t));
    memset(frame, 0, sizeof(interrupt_frame_t));

    frame->rip = proc->entry_point;
    frame->cs = GDT_USER_CODE_SEG | 3;
    frame->rflags = 0x202;
    frame->rsp = proc->user_stack_top;
    frame->ss = GDT_USER_DATA_SEG | 3;
    frame->vector = 0x20;

    proc->saved_rsp = (uint64_t)frame;

    process_table[slot] = proc;
    proc->state = PROCESS_READY;
    scheduler_add(proc);

    return proc;
}

/* ── Create Process from ELF Executable (Phase 6 Core) ────────────────────── */

process_t *process_create_from_elf(const char *path, char *const argv[]) {
    if (!path) return NULL;

    nyota_fs_t *fs = vfs_get_root_fs();
    if (!fs) {
        kwarn("process_create_from_elf: root filesystem not mounted");
        return NULL;
    }

    uint64_t inode_num = 0;
    if (nyotafs_resolve_path(fs, path, &inode_num) != 0) {
        kwarn("process_create_from_elf: failed to resolve path");
        return NULL;
    }

    int slot = -1;
    for (size_t i = 0; i < PROCESS_MAX_COUNT; i++) {
        if (!process_slot_in_use[i]) {
            slot = (int)i;
            break;
        }
    }
    if (slot == -1) {
        kwarn("process_create_from_elf: process table full");
        return NULL;
    }

    process_slot_in_use[slot] = true;
    process_t *proc = &process_table_storage[slot];
    memset(proc, 0, sizeof(process_t));

    process_t *curr = process_get_current();
    if (curr && curr->max_processes > 0) {
        uint32_t proc_cnt = 0;
        for (process_t *c = curr->children; c != NULL; c = c->next_sibling) {
            proc_cnt++;
        }
        if (proc_cnt >= curr->max_processes) {
            kwarn("process_create_from_elf: process limit exceeded");
            process_slot_in_use[slot] = false;
            return NULL;
        }
    }

    proc->pid = next_pid++;
    proc->parent_pid = curr ? curr->pid : 0;
    proc->state = PROCESS_NEW;
    proc->exit_status = 0;

    /* Extract process name from path */
    const char *pname = path;
    for (const char *p = path; *p != '\0'; p++) {
        if (*p == '/') pname = p + 1;
    }
    size_t plen = strlen(pname);
    if (plen >= PROCESS_NAME_MAX) plen = PROCESS_NAME_MAX - 1;
    memcpy(proc->name, pname, plen);
    proc->name[plen] = '\0';

    /* Initialize file descriptors */
    vfs_init_process_fds(proc->fds);

    /* Assign dedicated 16 KiB static kernel stack */
    proc->kernel_stack = (uint64_t)&process_kernel_stacks[slot][0];
    proc->kernel_stack_top = proc->kernel_stack + sizeof(process_kernel_stacks[slot]);

    /* Create address space */
    page_table_t *pml4 = paging_create_address_space();
    if (!pml4) {
        process_slot_in_use[slot] = false;
        return NULL;
    }
    proc->cr3 = (uint64_t)pml4;

    /* ASLR Foundation: randomize user stack top by 0 to 31 pages */
    uint64_t aslr_offset = (kernel_random() & 0x1FULL) * PAGE_SIZE;
    uint64_t stack_top = USER_STACK_TOP - aslr_offset;
    uint64_t stack_base = stack_top - USER_STACK_SIZE;

    /* Map user stack pages (16 KiB = 4 pages).
     * The page immediately below stack_base is left unmapped as a guard page! */
    for (uint64_t sp = stack_base; sp < stack_top; sp += PAGE_SIZE) {
        void *stack_phys = pmm_alloc_page();
        if (!stack_phys) {
            process_slot_in_use[slot] = false;
            return NULL;
        }
        memset(stack_phys, 0, PAGE_SIZE);
        paging_map_page_in(pml4, sp, (uint64_t)stack_phys, PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE);
    }

    /* Load ELF program segments into process address space */
    uint64_t entry_point = 0;
    int elf_status = elf_load_executable(fs, inode_num, pml4, &entry_point);
    if (elf_status != NYOTA_OK) {
        kwarn("process_create_from_elf: elf_load_executable failed");
        process_slot_in_use[slot] = false;
        return NULL;
    }

    proc->entry_point = entry_point;

    /* Setup argc / argv / envp on user stack */
    int argc = 0;
    uint64_t user_argv = 0;
    uint64_t user_envp = 0;
    uint64_t user_rsp = setup_user_stack(pml4, stack_top, path, argv, NULL, &argc, &user_argv, &user_envp);
    if (user_rsp == 0) {
        user_rsp = stack_top - 16;
    }
    proc->user_stack_top = user_rsp;

    /* Inherit credentials, permissions, working dir & resource limits */
    if (curr) {
        process_add_child(curr, proc);
        proc->uid = curr->uid;
        proc->gid = curr->gid;
        proc->capabilities = curr->capabilities;
        proc->max_memory = curr->max_memory ? curr->max_memory : (64 * 1024 * 1024ULL);
        proc->max_open_files = curr->max_open_files ? curr->max_open_files : MAX_PROCESS_FDS;
        proc->max_processes = curr->max_processes ? curr->max_processes : 16;
        proc->max_sockets = curr->max_sockets ? curr->max_sockets : 16;
        proc->pgrp = curr->pgrp ? curr->pgrp : proc->pid;
        if (curr->cwd[0] != '\0') {
            size_t clen = strlen(curr->cwd);
            if (clen >= sizeof(proc->cwd)) clen = sizeof(proc->cwd) - 1;
            memcpy(proc->cwd, curr->cwd, clen);
            proc->cwd[clen] = '\0';
        } else {
            proc->cwd[0] = '/';
            proc->cwd[1] = '\0';
        }
    } else {
        proc->parent = NULL;
        proc->parent_pid = 0;
        proc->uid = 0;
        proc->gid = 0;
        proc->capabilities = 0xFFFFFFFFFFFFFFFFULL;
        proc->max_memory = 64 * 1024 * 1024ULL;
        proc->max_open_files = MAX_PROCESS_FDS;
        proc->max_processes = 16;
        proc->max_sockets = 16;
        proc->pgrp = proc->pid;
        proc->cwd[0] = '/';
        proc->cwd[1] = '\0';
    }
    proc->start_time = timer_uptime_sec();

    signal_init_process(proc);
    shm_process_init(proc);

    /* Fabricate initial interrupt_frame_t */
    interrupt_frame_t *frame = (interrupt_frame_t *)(proc->kernel_stack_top - sizeof(interrupt_frame_t));
    memset(frame, 0, sizeof(interrupt_frame_t));

    frame->rip = proc->entry_point;
    frame->cs = GDT_USER_CODE_SEG | 3;
    frame->rflags = 0x202;
    frame->rsp = proc->user_stack_top;
    frame->ss = GDT_USER_DATA_SEG | 3;
    frame->rdi = (uint64_t)argc;
    frame->rsi = user_argv;
    frame->rdx = user_envp;
    frame->vector = 0x20;

    proc->saved_rsp = (uint64_t)frame;

    process_table[slot] = proc;
    proc->state = PROCESS_READY;
    scheduler_add(proc);

    return proc;
}

process_t *process_spawn_elf(const char *path, char *const argv[]) {
    return process_create_from_elf(path, argv);
}

process_t *process_spawn_elf_redirect(const char *path, char *const argv[], int in_fd, int out_fd) {
    process_t *proc = process_create_from_elf(path, argv);
    if (!proc) return NULL;

    process_t *curr = process_get_current();
    if (curr) {
        if (in_fd >= 0 && in_fd < MAX_PROCESS_FDS && curr->fds[in_fd]) {
            if (proc->fds[0]) {
                file_t *old = proc->fds[0];
                proc->fds[0] = NULL;
                vfs_close_file(old);
            }
            proc->fds[0] = curr->fds[in_fd];
            curr->fds[in_fd]->ref_count++;
        }
        if (out_fd >= 0 && out_fd < MAX_PROCESS_FDS && curr->fds[out_fd]) {
            if (proc->fds[1]) {
                file_t *old = proc->fds[1];
                proc->fds[1] = NULL;
                vfs_close_file(old);
            }
            proc->fds[1] = curr->fds[out_fd];
            curr->fds[out_fd]->ref_count++;
        }
    }
    return proc;
}

int process_execve(process_t *proc, const char *path, char *const argv[], char *const envp[]) {
    if (!proc || !path) return NYOTA_EINVAL;

    nyota_fs_t *fs = vfs_get_root_fs();
    if (!fs) return NYOTA_ENODEV;

    uint64_t inode_num = 0;
    if (nyotafs_resolve_path(fs, path, &inode_num) != 0) {
        return NYOTA_ENOENT;
    }

    /* Create replacement address space */
    page_table_t *new_pml4 = paging_create_address_space();
    if (!new_pml4) return NYOTA_ENOMEM;

    /* ASLR Stack top */
    uint64_t aslr_offset = (kernel_random() & 0x1FULL) * PAGE_SIZE;
    uint64_t stack_top = USER_STACK_TOP - aslr_offset;
    uint64_t stack_base = stack_top - USER_STACK_SIZE;

    /* Map stack in new address space (with guard page below stack_base) */
    for (uint64_t sp = stack_base; sp < stack_top; sp += PAGE_SIZE) {
        void *stack_phys = pmm_alloc_page();
        if (!stack_phys) return NYOTA_ENOMEM;
        memset(stack_phys, 0, PAGE_SIZE);
        paging_map_page_in(new_pml4, sp, (uint64_t)stack_phys, PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE);
    }

    uint64_t entry_point = 0;
    int elf_status = elf_load_executable(fs, inode_num, new_pml4, &entry_point);
    if (elf_status != NYOTA_OK) {
        return elf_status;
    }

    /* Setup argc / argv / envp */
    int argc = 0;
    uint64_t user_argv = 0;
    uint64_t user_envp = 0;
    uint64_t user_rsp = setup_user_stack(new_pml4, stack_top, path, argv, envp, &argc, &user_argv, &user_envp);
    if (user_rsp == 0) user_rsp = stack_top - 16;

    /* Switch process to new address space */
    proc->cr3 = (uint64_t)new_pml4;
    proc->entry_point = entry_point;
    proc->user_stack_top = user_rsp;

    /* Extract new process name */
    const char *pname = path;
    for (const char *p = path; *p != '\0'; p++) {
        if (*p == '/') pname = p + 1;
    }
    size_t plen = strlen(pname);
    if (plen >= PROCESS_NAME_MAX) plen = PROCESS_NAME_MAX - 1;
    memcpy(proc->name, pname, plen);
    proc->name[plen] = '\0';

    if (proc == process_get_current()) {
        paging_load_cr3(proc->cr3);
    }

    /* Update saved interrupt frame */
    interrupt_frame_t *frame = (interrupt_frame_t *)proc->saved_rsp;
    if (frame) {
        frame->rip = entry_point;
        frame->rsp = user_rsp;
        frame->rdi = (uint64_t)argc;
        frame->rsi = user_argv;
        frame->rdx = user_envp;
        frame->rax = 0;
    }

    return NYOTA_OK;
}

int process_exec(process_t *proc, const char *path, char *const argv[]) {
    return process_execve(proc, path, argv, NULL);
}

int process_waitpid(int32_t pid, int *status, int options) {
    process_t *curr = process_get_current();
    if (!curr) return -NYOTA_EINVAL;

    while (1) {
        bool has_children = false;
        process_t *target_child = NULL;

        for (process_t *c = curr->children; c != NULL; c = c->next_sibling) {
            if (pid == -1 || (int32_t)c->pid == pid) {
                has_children = true;
                if (c->state == PROCESS_ZOMBIE || c->state == PROCESS_TERMINATED) {
                    target_child = c;
                    break;
                }
            }
        }

        if (target_child) {
            int exit_val = target_child->exit_status;
            uint32_t reaped_pid = target_child->pid;
            if (status) {
                *status = exit_val;
            }

            process_remove_child(curr, target_child);

            for (size_t i = 0; i < PROCESS_MAX_COUNT; i++) {
                if (process_table[i] == target_child) {
                    process_table[i] = NULL;
                    process_slot_in_use[i] = false;
                    break;
                }
            }
            return (int)reaped_pid;
        }

        if (!has_children) {
            return -10; /* ECHILD */
        }

        /* If non-blocking requested, return 0 immediately */
        if (options & WNOHANG) {
            return 0;
        }

        /* Check for pending signals */
        if (curr->pending_signals & ~curr->blocked_signals) {
            return -4; /* EINTR */
        }

        /* Block until a child terminates and wakes us */
        curr->state = PROCESS_SLEEPING;
        scheduler_remove(curr);
        scheduler_request_reschedule();

        while (curr->state == PROCESS_SLEEPING) {
            __asm__ volatile ("sti; hlt");
        }
        curr->state = PROCESS_RUNNING;
    }
}

void process_exit(int status) {
    process_t *curr = process_get_current();
    if (curr) {
        curr->state = PROCESS_ZOMBIE;
        curr->exit_status = status;

        /* Close all open file descriptors */
        vfs_close_process_fds(curr->fds);

        /* Clean up attached shared memory */
        shm_process_cleanup(curr);

        /* Reparent orphaned children to PID 1 (init) */
        process_t *init_proc = process_find(1);
        while (curr->children) {
            process_t *child = curr->children;
            process_remove_child(curr, child);
            if (init_proc && init_proc != curr) {
                process_add_child(init_proc, child);
            }
        }

        /* Wake and notify parent */
        if (curr->parent) {
            signal_send(curr->parent, SIGCHLD);
            scheduler_wake(curr->parent);
        }

        /* Remove from scheduler ready queue */
        scheduler_remove(curr);
        scheduler_request_reschedule();

        /* Never return to userspace; wait for context switch */
        while (1) {
            __asm__ volatile ("sti; hlt");
        }
    }
}

/* ── Process Table Listing (ps command) ───────────────────────────────────── */

static const char *state_to_string(process_state_t st) {
    switch (st) {
        case PROCESS_NEW:        return "NEW";
        case PROCESS_READY:      return "READY";
        case PROCESS_RUNNING:    return "RUNNING";
        case PROCESS_SLEEPING:   return "SLEEPING";
        case PROCESS_ZOMBIE:     return "ZOMBIE";
        case PROCESS_TERMINATED: return "TERMINATED";
        case PROCESS_IDLE:       return "IDLE";
        default:                 return "UNKNOWN";
    }
}

void process_list(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_println("PID   PPID  UID   STATE       NAME");
    vga_println("----------------------------------------");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    for (size_t i = 0; i < PROCESS_MAX_COUNT; i++) {
        process_t *p = process_table[i];
        if (!p) continue;

        /* PID */
        vga_print_dec(p->pid);
        if (p->pid < 10) vga_print("     ");
        else if (p->pid < 100) vga_print("    ");
        else vga_print("   ");

        /* PPID */
        vga_print_dec(p->parent_pid);
        if (p->parent_pid < 10) vga_print("     ");
        else if (p->parent_pid < 100) vga_print("    ");
        else vga_print("   ");

        /* UID */
        vga_print_dec(p->uid);
        if (p->uid < 10) vga_print("     ");
        else if (p->uid < 100) vga_print("    ");
        else if (p->uid < 1000) vga_print("   ");
        else vga_print("  ");

        /* STATE */
        const char *st = state_to_string(p->state);
        vga_print(st);
        size_t slen = strlen(st);
        for (size_t s = slen; s < 12; s++) vga_print(" ");

        /* NAME */
        vga_println(p->name);
    }
}

int process_get_table(proc_info_t *out, size_t max_count) {
    if (!out || max_count == 0) return 0;
    size_t count = 0;
    for (size_t i = 0; i < PROCESS_MAX_COUNT && count < max_count; i++) {
        process_t *p = process_table[i];
        if (!p) continue;
        out[count].pid = p->pid;
        out[count].ppid = p->parent_pid;
        out[count].uid = p->uid;
        const char *st = state_to_string(p->state);
        size_t sidx = 0;
        while (sidx < sizeof(out[count].state) - 1 && st && st[sidx]) {
            out[count].state[sidx] = st[sidx];
            sidx++;
        }
        out[count].state[sidx] = '\0';

        size_t nidx = 0;
        while (nidx < sizeof(out[count].name) - 1 && p->name[nidx]) {
            out[count].name[nidx] = p->name[nidx];
            nidx++;
        }
        out[count].name[nidx] = '\0';
        count++;
    }
    return (int)count;
}

/* ── Launch First Userspace Process (/init) ───────────────────────────────── */

process_t *process_spawn_init(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print("[INFO]  ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_println("Loading /sbin/init");

    /* Try loading real ELF binary from canonical path /sbin/init */
    process_t *proc = process_create_from_elf("/sbin/init", NULL);
    if (!proc) {
        /* Fallback to /init */
        proc = process_create_from_elf("/init", NULL);
    }
    if (proc) {
        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        vga_print("[ OK ]  ");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        vga_println("ELF loaded");

        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        vga_print("[ OK ]  ");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        vga_print("PID ");
        vga_print_dec(proc->pid);
        vga_println(" started");

        return proc;
    }

    /* Fallback if /sbin/init and /init are not present on disk (Phase 5 fallback) */
    kwarn("process_spawn_init: /sbin/init not found on disk, falling back to embedded init");
    const void *blob = user_get_prog_a();
    proc = process_create("init", USER_CODE_BASE, blob, USER_PROGRAM_BLOB_SIZE);
    return proc;
}

void process_run(process_t *proc) {
    if (!proc) return;
    if (!scheduler_is_active()) {
        scheduler_start();
    }
    scheduler_yield();
}
