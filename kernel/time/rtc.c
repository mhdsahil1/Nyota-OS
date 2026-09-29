/* =============================================================================
 * Nyota OS — Real-Time Clock (RTC / CMOS) Driver
 * Interrogates Motorola 146818-compatible RTC via ports 0x70 / 0x71.
 * =========================================================================== */

#include "time/rtc.h"
#include "io.h"

#define CMOS_ADDR_PORT    0x70
#define CMOS_DATA_PORT    0x71

#define RTC_REG_SEC       0x00
#define RTC_REG_MIN       0x02
#define RTC_REG_HOUR      0x04
#define RTC_REG_DAY       0x07
#define RTC_REG_MONTH     0x08
#define RTC_REG_YEAR      0x09
#define RTC_REG_STAT_A    0x0A
#define RTC_REG_STAT_B    0x0B
#define RTC_REG_CENTURY   0x32

static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDR_PORT, (outb(CMOS_ADDR_PORT, reg), reg));
    io_wait();
    return inb(CMOS_DATA_PORT);
}

static bool rtc_is_updating(void) {
    outb(CMOS_ADDR_PORT, RTC_REG_STAT_A);
    io_wait();
    return (inb(CMOS_DATA_PORT) & 0x80) != 0;
}

static uint8_t bcd_to_bin(uint8_t val) {
    return ((val >> 4) * 10) + (val & 0x0F);
}

void rtc_init(void) {
    /* Basic probe to ensure RTC registers are readable */
    while (rtc_is_updating()) {
        /* spin */
    }
}

void rtc_read_datetime(rtc_time_t *out) {
    if (!out) return;

    /* Wait until RTC is not updating */
    while (rtc_is_updating()) {}

    uint8_t sec  = cmos_read(RTC_REG_SEC);
    uint8_t min  = cmos_read(RTC_REG_MIN);
    uint8_t hour = cmos_read(RTC_REG_HOUR);
    uint8_t day  = cmos_read(RTC_REG_DAY);
    uint8_t mon  = cmos_read(RTC_REG_MONTH);
    uint8_t year = cmos_read(RTC_REG_YEAR);
    uint8_t cent = cmos_read(RTC_REG_CENTURY);

    uint8_t stat_b = cmos_read(RTC_REG_STAT_B);

    /* If not binary mode, decode BCD */
    if (!(stat_b & 0x04)) {
        sec  = bcd_to_bin(sec);
        min  = bcd_to_bin(min);
        hour = ((hour & 0x7F) != hour) ? (bcd_to_bin(hour & 0x7F) | 0x80) : bcd_to_bin(hour);
        day  = bcd_to_bin(day);
        mon  = bcd_to_bin(mon);
        year = bcd_to_bin(year);
        cent = bcd_to_bin(cent);
    }

    /* Convert 12-hour clock to 24-hour if bit 1 of Status B is clear */
    if (!(stat_b & 0x02) && (hour & 0x80)) {
        hour = ((hour & 0x7F) + 12) % 24;
    }

    /* Full 4-digit year calculation */
    uint32_t full_year = 0;
    if (cent > 0) {
        full_year = (uint32_t)cent * 100 + year;
    } else {
        full_year = 2000 + year;
    }

    out->second = sec;
    out->minute = min;
    out->hour   = hour;
    out->day    = day;
    out->month  = mon;
    out->year   = full_year;
}

static bool is_leap_year(uint32_t y) {
    return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
}

static const uint32_t days_before_month[13] = {
    0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
};

uint64_t rtc_to_epoch_seconds(const rtc_time_t *dt) {
    if (!dt) return 0;

    uint32_t y = dt->year;
    uint32_t m = dt->month;
    uint32_t d = dt->day;

    if (m < 1) m = 1;
    if (m > 12) m = 12;
    if (d < 1) d = 1;

    /* Days from 1970 to year y */
    uint64_t days = 0;
    for (uint32_t cur = 1970; cur < y; cur++) {
        days += is_leap_year(cur) ? 366 : 365;
    }

    days += days_before_month[m];
    if (m > 2 && is_leap_year(y)) {
        days += 1;
    }

    days += (d - 1);

    uint64_t total_sec = days * 86400ULL;
    total_sec += (uint64_t)dt->hour * 3600ULL;
    total_sec += (uint64_t)dt->minute * 60ULL;
    total_sec += (uint64_t)dt->second;

    return total_sec;
}
