/* =============================================================================
 * Nyota OS — Kernel Symbol Table & Stack Frame Unwinder
 * Resolves instruction pointers to symbol names and walks RBP stack chains.
 * =========================================================================== */

#include "debug/ksym.h"
#include "vga.h"
#include "serial.h"
#include "types.h"

/* Extern symbol forward declarations */
extern void kernel_entry(void);
extern void kernel_main(void);
extern void gdt_init(void);
extern void gdt_flush(void);
extern void tss_init(void);
extern void idt_init(void);
extern void exceptions_init(void);
extern void panic_with_frame(const char *title, const void *frame);
extern void exception_handler(void *frame);
extern void syscall_init(void);
extern void syscall_handler(void *frame);
extern void pic_init(void);
extern void timer_init(void);
extern void lapic_init(void);
extern void smp_init(void);
extern void pmm_init(void);
extern void pmm_alloc_page(void);
extern void pmm_free_page(void *addr);
extern void paging_init(void);
extern void heap_init(void);
extern void kmalloc(size_t size);
extern void kfree(void *ptr);
extern void process_system_init(void);
extern void process_create(void);
extern void process_exit(int status);
extern void scheduler_init(void);
extern void scheduler_schedule(void);
extern void scheduler_yield(void);
extern void signal_check_and_deliver(void *frame);
extern void security_init(void);
extern void pipe_init(void);
extern void shm_init(void);
extern void vfs_init(void);
extern void elf_load_executable(void);
extern void net_init(void);
extern void netdev_init(void);
extern void e1000_init(void);
extern void e1000_send(void);
extern void e1000_poll_rx(void);
extern void netdev_receive(void);
extern void socket_init(void);
extern void vmm_init(void);
extern void vmx_init_host(void);
extern void svm_init_host(void);
extern void ept_build_guest_mapping(void);
extern void vga_init(void);
extern void serial_init(void);
extern void keyboard_init(void);

static ksym_entry_t static_symbols[] = {
    { (uint64_t)&kernel_entry,          "kernel_entry" },
    { (uint64_t)&kernel_main,           "kernel_main" },
    { (uint64_t)&gdt_init,              "gdt_init" },
    { (uint64_t)&gdt_flush,             "gdt_flush" },
    { (uint64_t)&tss_init,              "tss_init" },
    { (uint64_t)&idt_init,              "idt_init" },
    { (uint64_t)&exceptions_init,       "exceptions_init" },
    { (uint64_t)&panic_with_frame,      "panic_with_frame" },
    { (uint64_t)&exception_handler,     "exception_handler" },
    { (uint64_t)&syscall_init,          "syscall_init" },
    { (uint64_t)&syscall_handler,       "syscall_handler" },
    { (uint64_t)&pic_init,              "pic_init" },
    { (uint64_t)&timer_init,            "timer_init" },
    { (uint64_t)&lapic_init,            "lapic_init" },
    { (uint64_t)&smp_init,              "smp_init" },
    { (uint64_t)&pmm_init,              "pmm_init" },
    { (uint64_t)&pmm_alloc_page,        "pmm_alloc_page" },
    { (uint64_t)&pmm_free_page,         "pmm_free_page" },
    { (uint64_t)&paging_init,           "paging_init" },
    { (uint64_t)&heap_init,             "heap_init" },
    { (uint64_t)&kmalloc,               "kmalloc" },
    { (uint64_t)&kfree,                 "kfree" },
    { (uint64_t)&process_system_init,   "process_system_init" },
    { (uint64_t)&process_create,        "process_create" },
    { (uint64_t)&process_exit,          "process_exit" },
    { (uint64_t)&scheduler_init,        "scheduler_init" },
    { (uint64_t)&scheduler_schedule,    "scheduler_schedule" },
    { (uint64_t)&scheduler_yield,       "scheduler_yield" },
    { (uint64_t)&signal_check_and_deliver, "signal_check_and_deliver" },
    { (uint64_t)&security_init,         "security_init" },
    { (uint64_t)&pipe_init,             "pipe_init" },
    { (uint64_t)&shm_init,              "shm_init" },
    { (uint64_t)&vfs_init,              "vfs_init" },
    { (uint64_t)&elf_load_executable,   "elf_load_executable" },
    { (uint64_t)&net_init,              "net_init" },
    { (uint64_t)&netdev_init,           "netdev_init" },
    { (uint64_t)&e1000_init,            "e1000_init" },
    { (uint64_t)&e1000_send,            "e1000_send" },
    { (uint64_t)&e1000_poll_rx,         "e1000_poll_rx" },
    { (uint64_t)&netdev_receive,        "netdev_receive" },
    { (uint64_t)&socket_init,           "socket_init" },
    { (uint64_t)&vmm_init,              "vmm_init" },
    { (uint64_t)&vmx_init_host,         "vmx_init_host" },
    { (uint64_t)&svm_init_host,         "svm_init_host" },
    { (uint64_t)&ept_build_guest_mapping, "ept_build_guest_mapping" },
    { (uint64_t)&vga_init,              "vga_init" },
    { (uint64_t)&serial_init,           "serial_init" },
    { (uint64_t)&keyboard_init,         "keyboard_init" }
};

#define STATIC_SYM_COUNT (sizeof(static_symbols) / sizeof(static_symbols[0]))

static bool ksym_sorted = false;

/* Simple bubble sort to sort symbols by ascending address */
void ksym_init(void) {
    for (size_t i = 0; i < STATIC_SYM_COUNT - 1; i++) {
        for (size_t j = 0; j < STATIC_SYM_COUNT - i - 1; j++) {
            if (static_symbols[j].addr > static_symbols[j + 1].addr) {
                ksym_entry_t tmp = static_symbols[j];
                static_symbols[j] = static_symbols[j + 1];
                static_symbols[j + 1] = tmp;
            }
        }
    }
    ksym_sorted = true;
}

const char *ksym_lookup(uint64_t addr, uint64_t *offset) {
    if (addr < 0x100000ULL) {
        if (offset) *offset = addr;
        return "bios/bootloader";
    }
    if (addr >= 0x0000800000000000ULL && addr < 0x0000801000000000ULL) {
        if (offset) *offset = addr - 0x0000800000000000ULL;
        return "userland_code";
    }

    if (!ksym_sorted) {
        ksym_init();
    }

    const char *best_name = NULL;
    uint64_t best_addr = 0;

    for (size_t i = 0; i < STATIC_SYM_COUNT; i++) {
        if (static_symbols[i].addr <= addr) {
            if (static_symbols[i].addr >= best_addr) {
                best_addr = static_symbols[i].addr;
                best_name = static_symbols[i].name;
            }
        }
    }

    if (best_name && (addr - best_addr < 0x8000ULL)) {
        if (offset) *offset = addr - best_addr;
        return best_name;
    }

    if (offset) *offset = 0;
    return "kernel_unknown";
}

void stack_trace_dump(uint64_t rbp, uint64_t rip) {
    vga_println("--- Kernel Stack Backtrace ---");
    serial_write("\n--- Kernel Stack Backtrace ---\n");

    uint64_t cur_rip = rip;
    uint64_t cur_rbp = rbp;
    int frame_num = 0;

    while (frame_num < 16) {
        uint64_t offset = 0;
        const char *name = ksym_lookup(cur_rip, &offset);

        /* VGA output */
        vga_print("  #");
        vga_print_dec(frame_num);
        vga_print(" 0x");
        vga_print_hex(cur_rip);
        vga_print(" in ");
        vga_print(name ? name : "??");
        if (offset > 0) {
            vga_print("+0x");
            vga_print_hex(offset);
        }
        vga_print(" (rbp=");
        vga_print_hex(cur_rbp);
        vga_println(")");

        /* Serial output */
        serial_write("  #");
        serial_write_dec(frame_num);
        serial_write(" 0x");
        serial_write_hex(cur_rip);
        serial_write(" in ");
        serial_write(name ? name : "??");
        if (offset > 0) {
            serial_write("+0x");
            serial_write_hex(offset);
        }
        serial_write(" (rbp=");
        serial_write_hex(cur_rbp);
        serial_write(")\n");

        frame_num++;

        /* Sanity checks on RBP */
        if (cur_rbp == 0 || (cur_rbp & 7) != 0) break;
        if (cur_rbp < 0x1000ULL) break;
        if (cur_rbp >= 0x0000800000000000ULL && cur_rbp < 0xFFFF800000000000ULL) break;

        /* Safely dereference frame */
        uint64_t *frame_ptr = (uint64_t *)cur_rbp;
        uint64_t next_rbp = frame_ptr[0];
        uint64_t ret_rip  = frame_ptr[1];

        if (ret_rip == 0) break;
        if (next_rbp <= cur_rbp && next_rbp != 0) {
            /* Detected stack loop or corruption */
            break;
        }

        cur_rip = ret_rip;
        cur_rbp = next_rbp;
    }
}

void stack_top_dump(uint64_t rsp, size_t count) {
    if (rsp == 0 || (rsp & 7) != 0) return;
    if (rsp < 0x1000ULL || (rsp >= 0x0000800000000000ULL && rsp < 0xFFFF800000000000ULL)) return;

    vga_println("--- Stack Top (QWORDs) ---");
    serial_write("--- Stack Top (QWORDs) ---\n");

    uint64_t *stack_ptr = (uint64_t *)rsp;
    for (size_t i = 0; i < count && i < 8; i++) {
        vga_print("  [RSP+0x");
        vga_print_hex(i * 8);
        vga_print("]: 0x");
        vga_print_hex(stack_ptr[i]);
        vga_println("");

        serial_write("  [RSP+0x");
        serial_write_hex(i * 8);
        serial_write("]: 0x");
        serial_write_hex(stack_ptr[i]);
        serial_write("\n");
    }
}
