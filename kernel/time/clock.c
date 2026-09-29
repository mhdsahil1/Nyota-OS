/* =============================================================================
 * Nyota OS — Kernel Time & Clock Subsystem Implementation
 * Real-time clock calibration, monotonic & wall-clock resolution.
 * =========================================================================== */

#include "time/clock.h"
#include "time/rtc.h"
#include "timer.h"
#include "serial.h"

static uint64_t base_epoch_seconds = 0;
static uint64_t base_tick = 0;
static rtc_time_t boot_datetime;

void clock_init(void) {
    rtc_init();
    rtc_read_datetime(&boot_datetime);
    base_epoch_seconds = rtc_to_epoch_seconds(&boot_datetime);
    base_tick = timer_ticks();

    serial_write("[CLOCK] RTC Wall-Clock initialized: ");
    serial_write_dec(boot_datetime.year);
    serial_write("-");
    if (boot_datetime.month < 10) serial_write("0");
    serial_write_dec(boot_datetime.month);
    serial_write("-");
    if (boot_datetime.day < 10) serial_write("0");
    serial_write_dec(boot_datetime.day);
    serial_write(" ");
    if (boot_datetime.hour < 10) serial_write("0");
    serial_write_dec(boot_datetime.hour);
    serial_write(":");
    if (boot_datetime.minute < 10) serial_write("0");
    serial_write_dec(boot_datetime.minute);
    serial_write(":");
    if (boot_datetime.second < 10) serial_write("0");
    serial_write_dec(boot_datetime.second);
    serial_write(" UTC (Epoch: ");
    serial_write_dec(base_epoch_seconds);
    serial_write(")\n");
}

void clock_on_timer_tick(void) {
    /* Timer tick hook - monotonic progression */
}

uint64_t clock_get_epoch_seconds(void) {
    uint64_t ticks = timer_ticks();
    uint64_t elapsed_ticks = (ticks >= base_tick) ? (ticks - base_tick) : 0;
    return base_epoch_seconds + (elapsed_ticks / PIT_DEFAULT_HZ);
}

uint64_t clock_get_monotonic_ms(void) {
    return (timer_ticks() * 1000ULL) / PIT_DEFAULT_HZ;
}

int clock_gettime(int clk_id, struct timespec *tp) {
    if (!tp) return -1;

    uint64_t ticks = timer_ticks();

    if (clk_id == CLOCK_REALTIME) {
        uint64_t elapsed_ticks = (ticks >= base_tick) ? (ticks - base_tick) : 0;
        tp->tv_sec = (int64_t)(base_epoch_seconds + (elapsed_ticks / PIT_DEFAULT_HZ));
        tp->tv_nsec = (int64_t)((elapsed_ticks % PIT_DEFAULT_HZ) * (1000000000ULL / PIT_DEFAULT_HZ));
        return 0;
    } else if (clk_id == CLOCK_MONOTONIC) {
        tp->tv_sec = (int64_t)(ticks / PIT_DEFAULT_HZ);
        tp->tv_nsec = (int64_t)((ticks % PIT_DEFAULT_HZ) * (1000000000ULL / PIT_DEFAULT_HZ));
        return 0;
    }

    return -1;
}

void clock_format_datetime(char *buf, size_t buf_sz) {
    if (!buf || buf_sz < 32) return;

    rtc_time_t cur;
    rtc_read_datetime(&cur);

    /* YYYY-MM-DD HH:MM:SS UTC */
    int pos = 0;
    uint32_t y = cur.year;
    buf[pos++] = '0' + ((y / 1000) % 10);
    buf[pos++] = '0' + ((y / 100) % 10);
    buf[pos++] = '0' + ((y / 10) % 10);
    buf[pos++] = '0' + (y % 10);
    buf[pos++] = '-';

    buf[pos++] = '0' + ((cur.month / 10) % 10);
    buf[pos++] = '0' + (cur.month % 10);
    buf[pos++] = '-';

    buf[pos++] = '0' + ((cur.day / 10) % 10);
    buf[pos++] = '0' + (cur.day % 10);
    buf[pos++] = ' ';

    buf[pos++] = '0' + ((cur.hour / 10) % 10);
    buf[pos++] = '0' + (cur.hour % 10);
    buf[pos++] = ':';

    buf[pos++] = '0' + ((cur.minute / 10) % 10);
    buf[pos++] = '0' + (cur.minute % 10);
    buf[pos++] = ':';

    buf[pos++] = '0' + ((cur.second / 10) % 10);
    buf[pos++] = '0' + (cur.second % 10);

    buf[pos++] = ' ';
    buf[pos++] = 'U';
    buf[pos++] = 'T';
    buf[pos++] = 'C';
    buf[pos] = '\0';
}
