/* =============================================================================
 * Nyota OS — CPU Exception Handlers & Diagnostic Kernel Panic
 * Handles x86_64 CPU exceptions (vectors 0..31)
 * =========================================================================== */

#include "exceptions.h"
#include "interrupts.h"
#include "kernel.h"
#include "vga.h"
#include "serial.h"
#include "process.h"
#include "signal.h"
#include "security/security.h"

static const char * const exception_names[32] = {
    "Divide Error (#DE)",
    "Debug Exception (#DB)",
    "Non-Maskable Interrupt (NMI)",
    "Breakpoint (#BP)",
    "Overflow (#OF)",
    "BOUND Range Exceeded (#BR)",
    "Invalid Opcode (#UD)",
    "Device Not Available (#NM)",
    "Double Fault (#DF)",
    "Coprocessor Segment Overrun",
    "Invalid TSS (#TS)",
    "Segment Not Present (#NP)",
    "Stack-Segment Fault (#SS)",
    "General Protection Fault (#GP)",
    "Page Fault (#PF)",
    "Reserved Vector 15",
    "x87 FPU Floating-Point Error (#MF)",
    "Alignment Check (#AC)",
    "Machine Check (#MC)",
    "SIMD Floating-Point Exception (#XM)",
    "Virtualization Exception (#VE)",
    "Control Protection Exception (#CP)",
    "Reserved Vector 22",
    "Reserved Vector 23",
    "Reserved Vector 24",
    "Reserved Vector 25",
    "Reserved Vector 26",
    "Reserved Vector 27",
    "Hypervisor Injection Exception (#HV)",
    "VMM Communication Exception (#VC)",
    "Security Exception (#SX)",
    "Reserved Vector 31"
};

void panic_with_frame(const char *title, const interrupt_frame_t *frame) {
    __asm__ volatile ("cli");

    vga_set_color(VGA_WHITE, VGA_RED);
    vga_println("");
    vga_println("========================================");
    vga_println("          NYOTA KERNEL PANIC            ");
    vga_println("========================================");

    if (title) {
        vga_print("Condition : ");
        vga_println(title);
    }

    if (frame) {
        const char *name = (frame->vector < 32) ? exception_names[frame->vector] : "Hardware/Software Interrupt";
        vga_print("Exception : ");
        vga_println(name);

        vga_print("Vector    : ");
        vga_print_dec(frame->vector);
        vga_println("");

        vga_print("Error Code: ");
        vga_print_hex(frame->error_code);
        vga_println("");

        if (frame->vector == 14) {
            uint64_t cr2;
            __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
            vga_print("CR2 (Addr): ");
            vga_print_hex(cr2);
            vga_println("");
        }

        vga_print("RIP       : ");
        vga_print_hex(frame->rip);
        vga_print("  CS: ");
        vga_print_hex(frame->cs);
        vga_println("");

        vga_print("RFLAGS    : ");
        vga_print_hex(frame->rflags);
        vga_println("");

        vga_print("RSP       : ");
        vga_print_hex(frame->rsp);
        vga_print("  SS: ");
        vga_print_hex(frame->ss);
        vga_println("");

        vga_println("----------------------------------------");
        vga_print("RAX: "); vga_print_hex(frame->rax);
        vga_print(" RBX: "); vga_print_hex(frame->rbx);
        vga_println("");
        vga_print("RCX: "); vga_print_hex(frame->rcx);
        vga_print(" RDX: "); vga_print_hex(frame->rdx);
        vga_println("");
        vga_print("RSI: "); vga_print_hex(frame->rsi);
        vga_print(" RDI: "); vga_print_hex(frame->rdi);
        vga_println("");
        vga_print("RBP: "); vga_print_hex(frame->rbp);
        vga_println("");
    }

    vga_println("========================================");
    vga_println("System halted.");

    while (1) {
        __asm__ volatile ("hlt");
    }
}

static void page_fault_handler(interrupt_frame_t *frame) {
    __asm__ volatile ("cli");

    uint64_t cr2;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));

    uint64_t err = frame->error_code;
    bool present = (err & 0x01) != 0;
    bool write   = (err & 0x02) != 0;
    bool user    = (err & 0x04) != 0;
    bool rsvd    = (err & 0x08) != 0;
    bool fetch   = (err & 0x10) != 0;
    bool pk      = (err & 0x20) != 0;

    /* If the fault occurred in Ring 3 User Space, terminate offending process without crashing kernel */
    if (user || (frame->cs & 3) == 3) {
        process_t *curr = process_get_current();
        uint32_t pid = curr ? curr->pid : 0;

        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_print("\n[SEC] PID "); vga_print_dec(pid); vga_println("");
        vga_println("      exception: page fault");
        vga_print("      address: "); vga_print_hex(cr2); vga_println("");
        vga_println("      signal: SIGSEGV\n");
        vga_print("[PROC] PID "); vga_print_dec(pid); vga_println(" terminated");
        vga_set_color(VGA_WHITE, VGA_BLACK);

        serial_write("\n[SEC] PID "); serial_write_dec(pid); serial_write("\n");
        serial_write("      exception: page fault\n");
        serial_write("      address: "); serial_write_hex(cr2); serial_write("\n");
        serial_write("      signal: SIGSEGV\n\n");
        serial_write("[PROC] PID "); serial_write_dec(pid); serial_write(" terminated\n");

        security_log("Page fault exception in user process", pid);

        if (curr && curr->signal_handlers[SIGSEGV] != 0 && curr->signal_handlers[SIGSEGV] != 1) {
            signal_send(curr, SIGSEGV);
            signal_check_and_deliver(frame);
            return;
        }

        interrupts_enable();
        process_exit(128 + SIGSEGV);
        return;
    }

    /* Kernel Page Fault Diagnostic Dump */
    serial_write("\n========================================\n");
    serial_write("           NYOTA PAGE FAULT             \n");
    serial_write("========================================\n");
    serial_write("Error Code : "); serial_write_hex(err); serial_write("\n");
    serial_write("Fault Addr : "); serial_write_hex(cr2); serial_write("\n");
    serial_write("RIP        : "); serial_write_hex(frame->rip); serial_write("\n");
    serial_write("Access     : "); serial_write(fetch ? "EXECUTE\n" : (write ? "WRITE\n" : "READ\n"));
    serial_write("Cause      : "); serial_write(rsvd ? "RESERVED BIT\n" : (pk ? "PROTECTION KEY\n" : (present ? "PROTECTION VIOLATION\n" : "NOT PRESENT\n")));
    serial_write("Mode       : KERNEL\n");
    serial_write("========================================\n");
    serial_write("RAX: "); serial_write_hex(frame->rax);
    serial_write(" RBX: "); serial_write_hex(frame->rbx);
    serial_write(" RCX: "); serial_write_hex(frame->rcx);
    serial_write(" RDX: "); serial_write_hex(frame->rdx); serial_write("\n");
    serial_write("RSI: "); serial_write_hex(frame->rsi);
    serial_write(" RDI: "); serial_write_hex(frame->rdi);
    serial_write(" RBP: "); serial_write_hex(frame->rbp);
    serial_write(" RSP: "); serial_write_hex(frame->rsp); serial_write("\n");

    /* Format on VGA */
    vga_set_color(VGA_WHITE, VGA_RED);
    vga_println("");
    vga_println("+========================================+");
    vga_println("|          NYOTA PAGE FAULT              |");
    vga_println("+========================================+");

    vga_print("| Error Code : ");
    vga_print_hex(err);
    vga_println("        |");

    vga_print("| Fault Addr : ");
    vga_print_hex(cr2);
    vga_println("|");

    vga_print("| RIP        : ");
    vga_print_hex(frame->rip);
    vga_println("|");

    vga_print("| Mode       : ");
    vga_print(user ? "USER                     |" : "KERNEL                   |");
    vga_println("");

    vga_print("| Access     : ");
    if (fetch) {
        vga_print("EXECUTE                  |");
    } else if (write) {
        vga_print("WRITE                    |");
    } else {
        vga_print("READ                     |");
    }
    vga_println("");

    vga_print("| Cause      : ");
    if (rsvd) {
        vga_print("RESERVED BIT             |");
    } else if (pk) {
        vga_print("PROTECTION KEY           |");
    } else if (present) {
        vga_print("PROTECTION VIOLATION     |");
    } else {
        vga_print("NOT PRESENT              |");
    }
    vga_println("");

    vga_println("+========================================+");
    vga_println("");
    vga_print("RSP: "); vga_print_hex(frame->rsp);
    vga_print("  RFLAGS: "); vga_print_hex(frame->rflags);
    vga_println("");
    vga_println("System halted.");

    while (1) {
        __asm__ volatile ("hlt");
    }
}

void exception_handler(interrupt_frame_t *frame) {
    if (!frame) return;

    /* Vector 3: Breakpoint (INT3) — non-fatal debug trap */
    if (frame->vector == 3) {
        vga_set_color(VGA_YELLOW, VGA_BLACK);
        vga_print("[DEBUG] Trapped Breakpoint (#BP) at RIP: ");
        vga_print_hex(frame->rip);
        vga_println("");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }

    /* Vector 14: Page Fault (#PF) — specialized diagnostic display */
    if (frame->vector == 14) {
        page_fault_handler(frame);
        return;
    }

    /* If exception occurred in Ring 3 User Space, convert to signal and terminate cleanly */
    if ((frame->cs & 3) == 3) {
        process_t *curr = process_get_current();
        uint32_t pid = curr ? curr->pid : 0;
        int sig = SIGSEGV;
        const char *exc_name = "general protection";

        if (frame->vector == 0) {
            sig = SIGFPE;
            exc_name = "divide error";
        } else if (frame->vector == 6) {
            sig = SIGILL;
            exc_name = "invalid opcode";
        }

        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_print("\n[SEC] PID "); vga_print_dec(pid); vga_println("");
        vga_print("      exception: "); vga_println(exc_name);
        vga_print("      signal: "); vga_println(sig == SIGFPE ? "SIGFPE" : (sig == SIGILL ? "SIGILL" : "SIGSEGV"));
        vga_println("");
        vga_print("[PROC] PID "); vga_print_dec(pid); vga_println(" terminated");
        vga_set_color(VGA_WHITE, VGA_BLACK);

        security_log("Exception in user process", pid);

        if (curr && curr->signal_handlers[sig] != 0 && curr->signal_handlers[sig] != 1) {
            signal_send(curr, sig);
            signal_check_and_deliver(frame);
            return;
        }

        interrupts_enable();
        process_exit(128 + sig);
        return;
    }

    /* All other CPU exceptions are fatal in early kernel phase */
    const char *name = (frame->vector < 32) ? exception_names[frame->vector] : "CPU Exception";
    panic_with_frame(name, frame);
}

void exceptions_init(void) {
    /* Register exception handlers for all 32 CPU exceptions */
    for (uint8_t i = 0; i < 32; i++) {
        interrupt_register_handler(i, exception_handler);
    }
}
