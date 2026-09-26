/* =============================================================================
 * Nyota OS — Programmable Interval Timer (PIT 8254) Driver
 * Drives system ticks, uptime calculation, and interrupt-driven timer sleep.
 * =========================================================================== */

#include "timer.h"
#include "interrupts.h"
#include "pic.h"
#include "io.h"

#define PIT_CHANNEL0_DATA   0x40
#define PIT_COMMAND_PORT    0x43
#define PIT_MODE_SQUARE_WAVE 0x36  /* Channel 0, Lobyte/Hibyte, Mode 3, Binary */

static volatile uint64_t timer_tick_count = 0;
static uint32_t timer_frequency = PIT_DEFAULT_HZ;

static void timer_irq_handler(interrupt_frame_t *frame) {
    (void)frame;
    timer_tick_count++;
}

void timer_init(uint32_t frequency_hz) {
    if (frequency_hz < 18) frequency_hz = 18;
    if (frequency_hz > 1000) frequency_hz = 1000;

    timer_frequency = frequency_hz;
    uint32_t divisor = PIT_BASE_FREQUENCY / frequency_hz;

    /* Configure Channel 0 for square wave mode */
    outb(PIT_COMMAND_PORT, PIT_MODE_SQUARE_WAVE);
    io_wait();

    /* Send frequency divisor: low byte then high byte */
    outb(PIT_CHANNEL0_DATA, (uint8_t)(divisor & 0xFF));
    io_wait();
    outb(PIT_CHANNEL0_DATA, (uint8_t)((divisor >> 8) & 0xFF));
    io_wait();

    /* Register IRQ0 handler with central interrupt dispatcher */
    interrupt_register_handler(IRQ_BASE_VECTOR + IRQ_TIMER, timer_irq_handler);

    /* Unmask IRQ0 on Master PIC */
    pic_unmask_irq(IRQ_TIMER);
}

uint64_t timer_ticks(void) {
    return timer_tick_count;
}

uint64_t timer_uptime_ms(void) {
    if (timer_frequency == 0) return 0;
    return (timer_tick_count * 1000) / timer_frequency;
}

uint64_t timer_uptime_sec(void) {
    if (timer_frequency == 0) return 0;
    return timer_tick_count / timer_frequency;
}

void timer_sleep(uint64_t milliseconds) {
    if (timer_frequency == 0) return;
    uint64_t ticks_to_wait = (milliseconds * timer_frequency) / 1000;
    if (ticks_to_wait == 0) ticks_to_wait = 1;

    uint64_t target_tick = timer_tick_count + ticks_to_wait;
    while (timer_tick_count < target_tick) {
        __asm__ volatile ("hlt");
    }
}

void timer_format_uptime(char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size < 9) return; /* Needs at least "HH:MM:SS\0" (9 bytes) */

    uint64_t total_sec = timer_uptime_sec();
    uint32_t hours   = (uint32_t)((total_sec / 3600) % 100);
    uint32_t minutes = (uint32_t)((total_sec % 3600) / 60);
    uint32_t seconds = (uint32_t)(total_sec % 60);

    buffer[0] = '0' + (char)(hours / 10);
    buffer[1] = '0' + (char)(hours % 10);
    buffer[2] = ':';
    buffer[3] = '0' + (char)(minutes / 10);
    buffer[4] = '0' + (char)(minutes % 10);
    buffer[5] = ':';
    buffer[6] = '0' + (char)(seconds / 10);
    buffer[7] = '0' + (char)(seconds % 10);
    buffer[8] = '\0';
}
