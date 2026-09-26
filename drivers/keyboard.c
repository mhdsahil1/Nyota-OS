/* =============================================================================
 * Nyota OS — PS/2 Keyboard Driver & Scancode Set 1 Decoder
 * Decodes make/break scancodes, tracks modifiers, queues events in a ring buffer.
 * =========================================================================== */

#include "keyboard.h"
#include "interrupts.h"
#include "pic.h"
#include "io.h"

#define PS2_DATA_PORT       0x60
#define PS2_STATUS_PORT     0x64

/* Scancode Set 1 standard unshifted mapping (index = scancode 0..88) */
static const char scancode_set1_normal[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   /* Left Control */
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,   /* Left Shift */
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
    0,   /* Right Shift */
    '*',
    0,   /* Left Alt */
    ' ', /* Space */
    0,   /* Caps Lock */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* F1 - F10 */
    0,   /* Num Lock */
    0,   /* Scroll Lock */
    0,   /* Keypad 7 / Home */
    0,   /* Keypad 8 / Up */
    0,   /* Keypad 9 / PgUp */
    '-', /* Keypad - */
    0,   /* Keypad 4 / Left */
    0,   /* Keypad 5 */
    0,   /* Keypad 6 / Right */
    '+', /* Keypad + */
    0,   /* Keypad 1 / End */
    0,   /* Keypad 2 / Down */
    0,   /* Keypad 3 / PgDn */
    0,   /* Keypad 0 / Ins */
    0,   /* Keypad . / Del */
    0, 0, 0,
    0, 0 /* F11, F12 */
};

/* Scancode Set 1 shifted mapping (index = scancode 0..88) */
static const char scancode_set1_shifted[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,   /* Left Control */
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,   /* Left Shift */
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    0,   /* Right Shift */
    '*',
    0,   /* Left Alt */
    ' ', /* Space */
    0,   /* Caps Lock */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* F1 - F10 */
    0, 0, 0, 0, 0, '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* Modifier State */
static bool shift_pressed = false;
static bool lshift_pressed = false;
static bool rshift_pressed = false;
static bool ctrl_pressed  = false;
static bool alt_pressed   = false;
static bool caps_locked   = false;
static bool extended_mode = false;

/* Ring Buffer */
static key_event_t buffer[KEYBOARD_BUFFER_SIZE];
static volatile uint32_t buf_head = 0;
static volatile uint32_t buf_tail = 0;

static inline void ring_buffer_push(const key_event_t *event) {
    uint32_t next = (buf_head + 1) % KEYBOARD_BUFFER_SIZE;
    if (next != buf_tail) {
        buffer[buf_head] = *event;
        buf_head = next;
    }
}

static void keyboard_irq_handler(interrupt_frame_t *frame) {
    (void)frame;

    /* Read raw scancode from PS/2 Data Port */
    uint8_t scancode = inb(PS2_DATA_PORT);

    /* Check for Extended Scancode prefix (0xE0) */
    if (scancode == 0xE0) {
        extended_mode = true;
        return;
    }

    bool is_break = (scancode & 0x80) != 0;
    uint8_t code = scancode & 0x7F;

    key_event_t ev;
    ev.scancode  = scancode;
    ev.pressed   = !is_break;
    ev.released  = is_break;
    ev.special   = KEY_NONE;
    ev.character = 0;

    /* Handle extended keys (arrow keys, etc.) */
    if (extended_mode) {
        extended_mode = false;
        switch (code) {
            case 0x48: ev.special = KEY_UP; break;
            case 0x50: ev.special = KEY_DOWN; break;
            case 0x4B: ev.special = KEY_LEFT; break;
            case 0x4D: ev.special = KEY_RIGHT; break;
            case 0x47: ev.special = KEY_HOME; break;
            case 0x4F: ev.special = KEY_END; break;
            case 0x49: ev.special = KEY_PAGE_UP; break;
            case 0x51: ev.special = KEY_PAGE_DOWN; break;
            case 0x53: ev.special = KEY_DELETE; break;
            case 0x52: ev.special = KEY_INSERT; break;
            case 0x1D: ctrl_pressed = !is_break; break; /* Right Ctrl */
            case 0x38: alt_pressed = !is_break; break;  /* Right Alt */
            default: break;
        }
    } else {
        /* Standard modifier tracking */
        switch (code) {
            case 0x2A: /* Left Shift */
                lshift_pressed = !is_break;
                shift_pressed = lshift_pressed || rshift_pressed;
                break;
            case 0x36: /* Right Shift */
                rshift_pressed = !is_break;
                shift_pressed = lshift_pressed || rshift_pressed;
                break;
            case 0x1D: /* Left Ctrl */
                ctrl_pressed = !is_break;
                break;
            case 0x38: /* Left Alt */
                alt_pressed = !is_break;
                break;
            case 0x3A: /* Caps Lock (toggles on make) */
                if (!is_break) {
                    caps_locked = !caps_locked;
                }
                break;
            default:
                if (code < 128) {
                    char normal = scancode_set1_normal[code];
                    char shifted = scancode_set1_shifted[code];

                    if (normal >= 'a' && normal <= 'z') {
                        /* Letters: affected by both Shift and CapsLock */
                        if (shift_pressed ^ caps_locked) {
                            ev.character = shifted;
                        } else {
                            ev.character = normal;
                        }
                    } else {
                        /* Symbols/Digits: affected only by Shift */
                        if (shift_pressed) {
                            ev.character = shifted;
                        } else {
                            ev.character = normal;
                        }
                    }
                }
                break;
        }
    }

    ev.shift     = shift_pressed;
    ev.ctrl      = ctrl_pressed;
    ev.alt       = alt_pressed;
    ev.caps_lock = caps_locked;

    /* Push event into circular buffer */
    ring_buffer_push(&ev);
}

void keyboard_init(void) {
    /* Flush any pending bytes in keyboard controller output buffer */
    while (inb(PS2_STATUS_PORT) & 0x01) {
        inb(PS2_DATA_PORT);
    }

    /* Register IRQ1 handler with central interrupt dispatcher */
    interrupt_register_handler(IRQ_BASE_VECTOR + IRQ_KEYBOARD, keyboard_irq_handler);

    /* Unmask IRQ1 on Master PIC */
    pic_unmask_irq(IRQ_KEYBOARD);
}

bool keyboard_available(void) {
    return (buf_head != buf_tail);
}

bool keyboard_read_event(key_event_t *event) {
    if (buf_head == buf_tail) {
        return false;
    }
    if (event) {
        *event = buffer[buf_tail];
    }
    buf_tail = (buf_tail + 1) % KEYBOARD_BUFFER_SIZE;
    return true;
}

#include "serial.h"

char keyboard_getchar(void) {
    key_event_t ev;
    while (1) {
        if (keyboard_read_event(&ev)) {
            if (ev.pressed && ev.character != 0) {
                return ev.character;
            }
        }
        if (serial_has_data()) {
            char c = serial_getchar();
            if (c == '\r') c = '\n';
            return c;
        }
        __asm__ volatile ("hlt");
    }
}

int keyboard_getchar_nonblocking(void) {
    key_event_t ev;
    while (keyboard_read_event(&ev)) {
        if (ev.pressed && ev.character != 0) {
            return (int)(unsigned char)ev.character;
        }
    }
    if (serial_has_data()) {
        char c = serial_getchar();
        if (c == '\r') c = '\n';
        return (int)(unsigned char)c;
    }
    return -1;
}
