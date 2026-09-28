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

static process_t *process_table[PROCESS_MAX_COUNT] = {0};
static process_t process_table_storage[PROCESS_MAX_COUNT];
static uint8_t process_kernel_stacks[PROCESS_MAX_COUNT][16384] __attribute__((aligned(16)));
static bool process_slot_in_use[PROCESS_MAX_COUNT] = {false};
static uint32_t next_pid = 1;

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

static uint64_t setup_user_stack(page_table_t *pml4, const char *path, char *const argv[], int *out_argc, uint64_t *out_user_argv) {
    /* Determine top physical frame */
    uint64_t top_page_vaddr = USER_STACK_TOP - PAGE_SIZE;
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

    /* Stack pointer starts at top of stack page */
    uint64_t user_rsp = USER_STACK_TOP;
    uint32_t offset = PAGE_SIZE;

    uint64_t arg_user_addrs[32];

    /* Copy strings to stack in reverse */
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

    /* Map user stack pages (16 KiB = 4 pages) */
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

    /* Load ELF program segments into process address space */
    uint64_t entry_point = 0;
    int elf_status = elf_load_executable(fs, inode_num, pml4, &entry_point);
    if (elf_status != NYOTA_OK) {
        kwarn("process_create_from_elf: elf_load_executable failed");
        process_slot_in_use[slot] = false;
        return NULL;
    }

    proc->entry_point = entry_point;

    /* Setup argc / argv on user stack */
    int argc = 0;
    uint64_t user_argv = 0;
    uint64_t user_rsp = setup_user_stack(pml4, path, argv, &argc, &user_argv);
    if (user_rsp == 0) {
        user_rsp = USER_STACK_TOP - 16;
    }
    proc->user_stack_top = user_rsp;

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

int process_exec(process_t *proc, const char *path, char *const argv[]) {
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

    /* Map stack in new address space */
    uint64_t stack_base = USER_STACK_TOP - USER_STACK_SIZE;
    for (uint64_t sp = stack_base; sp < USER_STACK_TOP; sp += PAGE_SIZE) {
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

    /* Setup argc / argv */
    int argc = 0;
    uint64_t user_argv = 0;
    uint64_t user_rsp = setup_user_stack(new_pml4, path, argv, &argc, &user_argv);
    if (user_rsp == 0) user_rsp = USER_STACK_TOP - 16;

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
        frame->rax = 0;
    }

    return NYOTA_OK;
}

int process_waitpid(uint32_t pid, int *status) {
    process_t *child = process_find(pid);
    if (!child) return NYOTA_ENOENT;

    if (child->state != PROCESS_TERMINATED) {
        return 0; /* Process still running */
    }

    if (status) {
        *status = child->exit_status;
    }

    /* Reclaim process table slot */
    for (size_t i = 0; i < PROCESS_MAX_COUNT; i++) {
        if (process_table[i] == child) {
            process_table[i] = NULL;
            process_slot_in_use[i] = false;
            break;
        }
    }

    return (int)pid;
}

void process_exit(int status) {
    process_t *curr = process_get_current();
    if (curr) {
        curr->state = PROCESS_TERMINATED;
        curr->exit_status = status;

        /* Close all open file descriptors */
        vfs_close_process_fds(curr->fds);

        /* Print exit information */
        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        vga_print("[INFO] Process ");
        vga_print_dec(curr->pid);
        vga_print(" (");
        vga_print(curr->name);
        vga_print(") exited with status ");
        vga_print_dec((uint64_t)status);
        vga_println("");
        vga_set_color(VGA_WHITE, VGA_BLACK);

        /* Remove from scheduler ready queue */
        scheduler_remove(curr);
        scheduler_request_reschedule();
    }
}

/* ── Process Table Listing (ps command) ───────────────────────────────────── */

static const char *state_to_string(process_state_t st) {
    switch (st) {
        case PROCESS_NEW:        return "NEW";
        case PROCESS_READY:      return "READY";
        case PROCESS_RUNNING:    return "RUNNING";
        case PROCESS_SLEEPING:   return "SLEEPING";
        case PROCESS_TERMINATED: return "TERMINATED";
        case PROCESS_IDLE:       return "IDLE";
        default:                 return "UNKNOWN";
    }
}

void process_list(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_println("PID   STATE       RUNTIME      SWITCHES  NAME");
    vga_println("--------------------------------------------------");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    process_t *idle = scheduler_get_idle();
    if (idle) {
        vga_print_dec(idle->pid);
        vga_print("     ");
        vga_print(state_to_string(idle->state));
        vga_print("        ");
        vga_print_dec(idle->runtime_ticks);
        vga_print("        ");
        vga_print_dec(idle->context_switches);
        vga_print("         ");
        vga_println(idle->name);
    }

    for (size_t i = 0; i < PROCESS_MAX_COUNT; i++) {
        process_t *p = process_table[i];
        if (!p) continue;

        vga_print_dec(p->pid);
        if (p->pid < 10) vga_print("     ");
        else vga_print("    ");

        const char *st = state_to_string(p->state);
        vga_print(st);
        size_t slen = strlen(st);
        for (size_t s = slen; s < 12; s++) vga_print(" ");

        vga_print_dec(p->runtime_ticks);
        if (p->runtime_ticks < 10) vga_print("          ");
        else if (p->runtime_ticks < 100) vga_print("         ");
        else vga_print("        ");

        vga_print_dec(p->context_switches);
        if (p->context_switches < 10) vga_print("         ");
        else if (p->context_switches < 100) vga_print("        ");
        else vga_print("       ");

        vga_println(p->name);
    }
}

/* ── Launch First Userspace Process (/init) ───────────────────────────────── */

process_t *process_spawn_init(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print("[INFO]  ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_println("Loading /init");

    /* Try loading real ELF binary from filesystem */
    process_t *proc = process_create_from_elf("/init", NULL);
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

    /* Fallback if /init is not present on disk (Phase 5 fallback) */
    kwarn("process_spawn_init: /init not found on disk, falling back to embedded init");
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
