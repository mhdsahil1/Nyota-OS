/* =============================================================================
 * Nyota OS — System Date Utility (/bin/date)
 * Reads kernel wall-clock time and displays current calendar datetime in UTC.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t t = (uint64_t)ts.tv_sec;

    /* Civil calendar calculations from epoch seconds */
    uint64_t days = t / 86400ULL;
    uint64_t rem = t % 86400ULL;

    uint32_t hours = (uint32_t)(rem / 3600);
    uint32_t mins = (uint32_t)((rem % 3600) / 60);
    uint32_t secs = (uint32_t)(rem % 60);

    /* Civil date conversion */
    uint64_t z = days + 719468ULL;
    uint64_t era = z / 146097ULL;
    uint64_t doe = z - era * 146097ULL;
    uint64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    uint64_t y = yoe + era * 400;
    uint64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    uint64_t mp = (5 * doy + 2) / 153;
    uint64_t d = doy - (153 * mp + 2) / 5 + 1;
    uint64_t m = mp + (mp < 10 ? 3 : -9);
    y += (m <= 2);

    printf("%04d-%02d-%02d %02d:%02d:%02d UTC\n",
           (int)y, (int)m, (int)d, hours, mins, secs);

    return 0;
}
