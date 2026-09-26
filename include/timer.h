#ifndef NYOTA_TIMER_H
#define NYOTA_TIMER_H

#include "types.h"

#define PIT_DEFAULT_HZ      100     /* 100 Hz = 10 ms per tick */
#define PIT_BASE_FREQUENCY  1193182 /* 1.193182 MHz base oscillator */

/* Timer APIs */
void timer_init(uint32_t frequency_hz);
uint64_t timer_ticks(void);
uint64_t timer_uptime_ms(void);
uint64_t timer_uptime_sec(void);
void timer_sleep(uint64_t milliseconds);
void timer_format_uptime(char *buffer, size_t buffer_size);

#endif /* NYOTA_TIMER_H */
