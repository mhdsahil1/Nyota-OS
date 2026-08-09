/* =============================================================================
 * Nyota OS — PS/2 Keyboard Driver
 * Handles IRQ1 (keyboard interrupt), reads scan codes from port 0x60,
 * translates them to ASCII using the US QWERTY scancode set 1 table,
 * and buffers them in a 256-byte ring buffer.
 * =========================================================================== */

#include "keyboard.h"
#include "../idt/idt.h"
#include "../vga/vga.h"

#define KB_DATA_PORT  0x60
#define KB_BUF_SIZE   256

/* ── US QWERTY scancode set 1 → ASCII (make codes only, index = scancode) ── */
static const char sc_ascii[128] = {
/*00*/  0,
/*01*/  27,     /* Escape      */
/*02*/ '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=',
/*0E*/  '\b',   /* Backspace   */
/*0F*/  '\t',   /* Tab         */
/*10*/ 'q','w','e','r','t','y','u','i','o','p','[',']',
/*1C*/  '\n',   /* Enter       */
/*1D*/  0,      /* Left Ctrl   */
/*1E*/ 'a','s','d','f','g','h','j','k','l',';','\'','`',
/*2A*/  0,      /* Left Shift  */
/*2B*/ '\\',
/*2C*/ 'z','x','c','v','b','n','m',',','.','/',
/*36*/  0,      /* Right Shift */
/*37*/ '*',     /* Keypad *    */
/*38*/  0,      /* Left Alt    */
/*39*/ ' ',     /* Space       */
/* The rest (function keys, numpad, etc.) map to 0 */
       0,0,0,0,0,0,0,0,0,0,  /* 3A-43 */
       0,0,0,0,0,0,0,0,0,0,  /* 44-4D */
       0,0,0,0,0,0,0,0,0,0,  /* 4E-57 */
       0,0,0,0,0,0,0,0,0,0,  /* 58-61 */
       0,0,0,0,0,0,0,0,0,0,  /* 62-6B */
       0,0,0,0,0,0,0,0,0,0,  /* 6C-75 */
       0,0,0,0,0,0,0,0,0,0   /* 76-7F */
};

/* ── Ring buffer ──────────────────────────────────────────────────────────── */
static volatile char kb_buf[KB_BUF_SIZE];
static volatile int  kb_head = 0; /* Writer index (IRQ handler) */
static volatile int  kb_tail = 0; /* Reader index (shell)       */static volatile int  kb_debug_count = 0; /* Debug: key press counter */
/* ── Port I/O ─────────────────────────────────────────────────────────────── */
static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* ── IRQ1 handler ─────────────────────────────────────────────────────────── */
static void keyboard_handler(struct registers *r) {
    (void)r; /* Unused — we only care about the scancode */

    uint8_t sc = inb(KB_DATA_PORT);

    /* Bit 7 set = key release → ignore */
    if (sc & 0x80) return;

    char c = sc_ascii[sc & 0x7F];
    if (c != 0) {        kb_debug_count++;        int next = (kb_head + 1) % KB_BUF_SIZE;
        if (next != kb_tail) { /* Drop if buffer full */
            kb_buf[kb_head] = c;
            kb_head         = next;
        }
    }
}

/* ── Public: initialise keyboard driver ──────────────────────────────────── */
void keyboard_init(void) {
    idt_set_handler(33, keyboard_handler); /* IRQ1 → vector 33 */
}

/* ── Public: blocking read — wait until a key is available ───────────────── */
char keyboard_getchar(void) {
    /* Sleep the CPU until an interrupt fires and fills the buffer */
    while (kb_head == kb_tail) {
        __asm__ volatile ("sti; hlt");
    }
    char c      = kb_buf[kb_tail];
    kb_tail     = (kb_tail + 1) % KB_BUF_SIZE;
    return c;
}
/* ── Public: get debug counter (for diagnostics) ────────────────────── */
int keyboard_get_debug_count(void) {
    return kb_debug_count;
}