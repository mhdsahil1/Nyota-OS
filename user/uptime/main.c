/* =============================================================================
 * Nyota OS — Uptime Utility (/bin/uptime)
 * Displays elapsed system uptime using the monotonic clock.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        printf("uptime: failed to get monotonic clock\n");
        return 1;
    }

    uint64_t sec = (uint64_t)ts.tv_sec;
    uint32_t hours = (uint32_t)(sec / 3600);
    uint32_t mins = (uint32_t)((sec % 3600) / 60);
    uint32_t secs = (uint32_t)(sec % 60);

    printf("up %02d:%02d:%02d\n", hours, mins, secs);

    return 0;
}
