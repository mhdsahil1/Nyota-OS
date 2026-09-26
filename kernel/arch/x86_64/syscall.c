/* =============================================================================
 * Nyota OS — System Call Implementation & Dispatcher
 * Dispatches Vector 0x80 syscalls, enforces user validation, and isolates Ring 0.
 * =========================================================================== */

#include "syscall.h"
#include "process.h"
#include "paging.h"
#include "idt.h"
#include "vga.h"
#include "serial.h"
#include "memory.h"
#include "kernel.h"

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

    /* Locate active address space (process PML4 or current CR3) */
    process_t *proc = process_get_current();
    page_table_t *pml4 = proc ? (page_table_t *)proc->cr3 : NULL;
    if (!pml4) {
        uint64_t cr3 = paging_read_cr3();
        pml4 = (page_table_t *)(cr3 & PAGE_ENTRY_ADDR_MASK);
    }

    /* Verify all pages in the range are mapped in active page tables */
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

/* ── Syscall Handlers ─────────────────────────────────────────────────────── */

static int64_t sys_handle_write(uint64_t user_ptr, uint64_t len) {
    if (len == 0) return 0;
    if (len > 4096) return SYS_ERR_EINVAL;

    if (!user_validate_pointer((const void *)user_ptr, len, false)) {
        return SYS_ERR_EFAULT;
    }

    char kbuf[256];
    size_t remaining = len;
    const char *curr = (const char *)user_ptr;

    while (remaining > 0) {
        size_t chunk = (remaining > sizeof(kbuf) - 1) ? (sizeof(kbuf) - 1) : remaining;
        memcpy(kbuf, curr, chunk);
        kbuf[chunk] = '\0';

        /* Output to VGA terminal (which mirrors to serial COM1) */
        vga_print(kbuf);

        curr += chunk;
        remaining -= chunk;
    }

    return (int64_t)len;
}

static int64_t sys_handle_exit(int status) {
    process_exit(status);
    return 0;
}

static int64_t sys_handle_getpid(void) {
    process_t *curr = process_get_current();
    if (curr) {
        return curr->pid;
    }
    return 0;
}

/* ── Syscall Dispatcher ───────────────────────────────────────────────────── */

int64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;

    switch (num) {
        case SYS_WRITE:
            return sys_handle_write(a1, a2);

        case SYS_EXIT:
            return sys_handle_exit((int)a1);

        case SYS_GETPID:
            return sys_handle_getpid();

        default:
            return SYS_ERR_ENOSYS;
    }
}

void syscall_handler(interrupt_frame_t *frame) {
    if (!frame) return;

    /*
     * Syscall ABI (System V AMD64 register conventions):
     * RAX = Syscall Number
     * RDI = Argument 1
     * RSI = Argument 2
     * RDX = Argument 3
     * R10 = Argument 4
     * R8  = Argument 5
     * Return value placed in RAX
     */
    int64_t ret = syscall_dispatch(
        frame->rax,
        frame->rdi,
        frame->rsi,
        frame->rdx,
        frame->r10,
        frame->r8
    );

    frame->rax = (uint64_t)ret;
}

/* ── Initialization ───────────────────────────────────────────────────────── */

extern uint64_t isr_stub_table[IDT_ENTRIES];

void syscall_init(void) {
    /* Set IDT gate 0x80 as a User Trap Gate (DPL = 3) */
    idt_set_gate(0x80, isr_stub_table[0x80], 0x08, IDT_GATE_USER_TRAP);

    /* Register C handler for vector 0x80 */
    interrupt_register_handler(0x80, syscall_handler);
}

/* ── User-Mode Invocation Wrappers ────────────────────────────────────────── */

int64_t sys_write(const char *buf, size_t len) {
    int64_t ret;
    __asm__ volatile (
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov $0, %%rax\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"(buf), "r"(len)
        : "rax", "rdi", "rsi", "rcx", "r11", "memory"
    );
    return ret;
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
