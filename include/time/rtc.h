/* =============================================================================
 * Nyota OS — Real-Time Clock (RTC / CMOS) Interface
 * Hardware clock access, BCD conversion, and civil calendar epoch calculation.
 * =========================================================================== */

#ifndef NYOTA_TIME_RTC_H
#define NYOTA_TIME_RTC_H

#include "types.h"

typedef struct {
    uint32_t year;    /* 4-digit year, e.g. 2026 */
    uint32_t month;   /* 1 - 12 */
    uint32_t day;     /* 1 - 31 */
    uint32_t hour;    /* 0 - 23 */
    uint32_t minute;  /* 0 - 59 */
    uint32_t second;  /* 0 - 59 */
} rtc_time_t;

void rtc_init(void);
void rtc_read_datetime(rtc_time_t *out);
uint64_t rtc_to_epoch_seconds(const rtc_time_t *dt);

#endif /* NYOTA_TIME_RTC_H */
