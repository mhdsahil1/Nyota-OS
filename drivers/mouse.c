/* =============================================================================
 * Nyota OS — PS/2 Mouse Driver Implementation
 * 8042 Aux port initialization, packet decoding, and movement tracking.
 * =========================================================================== */

#include "drivers/mouse.h"
#include "drivers/input.h"
#include "drivers/framebuffer.h"
#include "interrupts.h"
#include "pic.h"
#include "io.h"
#include "serial.h"

#define PS2_DATA_PORT       0x60
#define PS2_STATUS_PORT     0x64

static mouse_state_t g_mouse = {
    .x = 512,
    .y = 384,
    .dx = 0,
    .dy = 0,
    .buttons = 0,
    .max_x = 1024,
    .max_y = 768
};

static uint8_t mouse_packet[3];
static uint8_t mouse_packet_idx = 0;

static inline void mouse_wait_write(void) {
    uint32_t timeout = 100000;
    while ((inb(PS2_STATUS_PORT) & 0x02) && --timeout) {
        io_wait();
    }
}

static inline void mouse_wait_read(void) {
    uint32_t timeout = 100000;
    while (!(inb(PS2_STATUS_PORT) & 0x01) && --timeout) {
        io_wait();
    }
}

static void mouse_write_cmd(uint8_t byte) {
    mouse_wait_write();
    outb(PS2_STATUS_PORT, 0xD4);
    mouse_wait_write();
    outb(PS2_DATA_PORT, byte);
}

static uint8_t mouse_read_data(void) {
    mouse_wait_read();
    return inb(PS2_DATA_PORT);
}

static void mouse_irq_handler(interrupt_frame_t *frame) {
    (void)frame;

    uint8_t status = inb(PS2_STATUS_PORT);
    if (!(status & 0x20)) {
        /* Not mouse data */
        pic_send_eoi(12);
        return;
    }

    uint8_t data = inb(PS2_DATA_PORT);

    /* Byte 0 must have bit 3 set to 1 for synchronization */
    if (mouse_packet_idx == 0 && !(data & 0x08)) {
        pic_send_eoi(12);
        return;
    }

    mouse_packet[mouse_packet_idx++] = data;

    if (mouse_packet_idx >= 3) {
        mouse_packet_idx = 0;

        uint8_t flags = mouse_packet[0];
        int32_t raw_x = (int8_t)mouse_packet[1];
        int32_t raw_y = (int8_t)mouse_packet[2];

        /* Discard on overflow */
        if (flags & 0xC0) {
            pic_send_eoi(12);
            return;
        }

        /* Calculate delta coordinates */
        int32_t dx = raw_x;
        int32_t dy = -raw_y; /* Invert Y axis: screen coordinates grow downward */

        g_mouse.dx = dx;
        g_mouse.dy = dy;
        g_mouse.x += dx;
        g_mouse.y += dy;

        /* Clamp to screen boundaries */
        if (g_mouse.x < 0) g_mouse.x = 0;
        if (g_mouse.x >= g_mouse.max_x) g_mouse.x = g_mouse.max_x - 1;
        if (g_mouse.y < 0) g_mouse.y = 0;
        if (g_mouse.y >= g_mouse.max_y) g_mouse.y = g_mouse.max_y - 1;

        uint32_t old_buttons = g_mouse.buttons;
        uint32_t new_buttons = 0;
        if (flags & 0x01) new_buttons |= MOUSE_BTN_LEFT;
        if (flags & 0x02) new_buttons |= MOUSE_BTN_RIGHT;
        if (flags & 0x04) new_buttons |= MOUSE_BTN_MIDDLE;
        g_mouse.buttons = new_buttons;

        /* Post mouse movement event */
        if (dx != 0 || dy != 0) {
            input_event_t ev = {
                .type = EVENT_MOUSE_MOVE,
                .keycode = 0,
                .character = 0,
                .mouse_x = g_mouse.x,
                .mouse_y = g_mouse.y,
                .mouse_dx = dx,
                .mouse_dy = dy,
                .mouse_buttons = new_buttons,
                .modifiers = 0,
                .window_id = 0,
                .timestamp = 0
            };
            input_post_event(&ev);
        }

        /* Post button down events */
        uint32_t pressed = new_buttons & ~old_buttons;
        if (pressed) {
            input_event_t ev = {
                .type = EVENT_MOUSE_BUTTON_PRESS,
                .keycode = 0,
                .character = 0,
                .mouse_x = g_mouse.x,
                .mouse_y = g_mouse.y,
                .mouse_dx = 0,
                .mouse_dy = 0,
                .mouse_buttons = pressed,
                .modifiers = 0,
                .window_id = 0,
                .timestamp = 0
            };
            input_post_event(&ev);
        }

        /* Post button up events */
        uint32_t released = old_buttons & ~new_buttons;
        if (released) {
            input_event_t ev = {
                .type = EVENT_MOUSE_BUTTON_RELEASE,
                .keycode = 0,
                .character = 0,
                .mouse_x = g_mouse.x,
                .mouse_y = g_mouse.y,
                .mouse_dx = 0,
                .mouse_dy = 0,
                .mouse_buttons = released,
                .modifiers = 0,
                .window_id = 0,
                .timestamp = 0
            };
            input_post_event(&ev);
        }
    }

    pic_send_eoi(12);
}

void mouse_set_bounds(int32_t max_x, int32_t max_y) {
    if (max_x > 0) g_mouse.max_x = max_x;
    if (max_y > 0) g_mouse.max_y = max_y;
    if (g_mouse.x >= g_mouse.max_x) g_mouse.x = g_mouse.max_x - 1;
    if (g_mouse.y >= g_mouse.max_y) g_mouse.y = g_mouse.max_y - 1;
}

mouse_state_t *mouse_get_state(void) {
    return &g_mouse;
}

void mouse_init(void) {
    framebuffer_t *fb = framebuffer_get_info();
    if (fb && fb->initialized) {
        g_mouse.max_x = fb->width;
        g_mouse.max_y = fb->height;
        g_mouse.x = fb->width / 2;
        g_mouse.y = fb->height / 2;
    }

    mouse_packet_idx = 0;

    /* 1. Enable Auxiliary PS/2 Device (Mouse) */
    mouse_wait_write();
    outb(PS2_STATUS_PORT, 0xA8);

    /* 2. Read PS/2 Controller Command Byte */
    mouse_wait_write();
    outb(PS2_STATUS_PORT, 0x20);
    uint8_t status = mouse_read_data();

    /* Enable IRQ 12 (bit 1) and enable mouse clock (clear bit 5) */
    status |= 0x02;
    status &= ~0x20;

    /* Write modified command byte back */
    mouse_wait_write();
    outb(PS2_STATUS_PORT, 0x60);
    mouse_wait_write();
    outb(PS2_DATA_PORT, status);

    /* 3. Send Reset / Defaults / Enable Streaming to Mouse */
    mouse_write_cmd(0xF6); /* Set defaults */
    mouse_read_data();     /* ACK (0xFA) */

    mouse_write_cmd(0xF4); /* Enable streaming packets */
    mouse_read_data();     /* ACK (0xFA) */

    /* 4. Register IRQ 12 Interrupt Handler and unmask in PIC */
    pic_unmask_irq(IRQ_CASCADE);
    pic_unmask_irq(IRQ_MOUSE);
    interrupt_register_handler(IRQ_BASE_VECTOR + IRQ_MOUSE, mouse_irq_handler);

    serial_write("[MOUSE] PS/2 Mouse driver initialized (IRQ 12)\n");
}
