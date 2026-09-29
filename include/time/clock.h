/* =============================================================================
 * Nyota OS — Kernel Time & Clock Subsystem Interface
 * Monotonic and wall-clock time tracking, POSIX timespec, and clock syscalls.
 * =========================================================================== */

#ifndef NYOTA_TIME_CLOCK_H
#define NYOTA_TIME_CLOCK_H

#include "types.h"

#define CLOCK_REALTIME      0
#define CLOCK_MONOTONIC     1

#ifndef _STRUCT_TIMESPEC_DEFINED
#define _STRUCT_TIMESPEC_DEFINED
struct timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};
typedef struct timespec timespec_t;
#endif

void clock_init(void);
void clock_on_timer_tick(void);

int clock_gettime(int clk_id, struct timespec *tp);
uint64_t clock_get_epoch_seconds(void);
uint64_t clock_get_monotonic_ms(void);
void clock_format_datetime(char *buf, size_t buf_sz);

#endif /* NYOTA_TIME_CLOCK_H */
