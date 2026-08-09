/* =============================================================================
 * Nyota OS — kernel_main
 * Entry point called from kernel_entry.asm.  Initialises all subsystems in
 * dependency order: VGA → GDT → IDT → Keyboard → Shell.
 * =========================================================================== */

#include <stdint.h>
#include <stddef.h>

#include "vga/vga.h"
#include "gdt/gdt.h"
#include "idt/idt.h"
#include "keyboard/keyboard.h"
#include "serial/serial.h"
#include "shell/shell.h"

/* ── Compiler-generated memset/memcpy stubs ───────────────────────────────
 * GCC may emit calls to these for struct/array initialisation even with
 * -fno-builtin.  Provide minimal freestanding implementations.          */
void *memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dst;
}

/* ── Kernel entry ─────────────────────────────────────────────────────────── */
void kernel_main(void) {
    /* 0. Initialize serial console first */
    serial_init();
    
    /* 1. Bring up the VGA text-mode terminal so we can print errors */
    vga_init();

    /* 2. Install the kernel's own GDT (replaces the bootloader's temporary GDT) */
    gdt_init();

    /* 3. Set up the IDT and remap the 8259 PIC */
    idt_init();

    /* 4. Register the PS/2 keyboard IRQ handler */
    keyboard_init();

    /* 5. Enable hardware interrupts — keyboard now works */
    __asm__ volatile ("sti");

    /* 6. Print the banner and enter the interactive shell loop */
    shell_init();
    shell_run();

    /* shell_run() loops forever; we should never reach here */
    __asm__ volatile ("cli; hlt");
}
