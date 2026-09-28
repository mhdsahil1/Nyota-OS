/* =============================================================================
 * Nyota OS — Central Interrupt Dispatcher
 * Dispatches CPU exceptions and hardware IRQs to registered C handlers.
 * =========================================================================== */

#include "interrupts.h"
#include "idt.h"
#include "exceptions.h"
#include "pic.h"
#include "kernel.h"
#include "vga.h"
#include "serial.h"

static interrupt_handler_t handlers[INTERRUPT_VECTOR_COUNT];

static void default_unhandled_handler(interrupt_frame_t *frame) {
    if (frame->vector < 32) {
        panic_with_frame("Unhandled CPU Exception", frame);
    }
    /* For unhandled IRQs (32..47), EOI will be sent centrally below */
}

void interrupt_register_handler(uint8_t vector, interrupt_handler_t handler) {
    handlers[vector] = handler;
}

void interrupt_unregister_handler(uint8_t vector) {
    handlers[vector] = default_unhandled_handler;
}

#include "scheduler.h"

interrupt_frame_t *interrupt_dispatch(interrupt_frame_t *frame) {
    if (!frame) return NULL;

    uint64_t vec = frame->vector;

    if (vec < INTERRUPT_VECTOR_COUNT && handlers[vec] != NULL) {
        handlers[vec](frame);
    } else {
        default_unhandled_handler(frame);
    }

    /* If this was a hardware IRQ (32..47), acknowledge via PIC EOI */
    if (vec >= IRQ_BASE_VECTOR && vec < (IRQ_BASE_VECTOR + IRQ_COUNT)) {
        pic_send_eoi((uint8_t)(vec - IRQ_BASE_VECTOR));
    }

    /* If scheduler is running, perform scheduling & context switch if needed */
    if (scheduler_is_active()) {
        frame = scheduler_schedule(frame);
    }

    return frame;
}

void interrupts_init(void) {
    /* 1. Reset all vector handlers to default */
    for (int i = 0; i < INTERRUPT_VECTOR_COUNT; i++) {
        handlers[i] = default_unhandled_handler;
    }

    /* 2. Initialize 8259 PIC (remaps IRQs 0..15 to vectors 32..47) */
    pic_init();

    /* 3. Initialize 256-entry IDT table and load IDTR */
    idt_init();

    /* 4. Register handlers for all CPU exceptions (0..31) */
    exceptions_init();
}
