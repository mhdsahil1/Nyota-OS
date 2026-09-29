/* =============================================================================
 * Nyota OS — Uptime Utility (/bin/uptime)
 * Displays elapsed system uptime and process count from kernel statistics.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    sysinfo_data_t info;
    if (sysinfo(&info) != 0) {
        printf("uptime: failed to get system info\n");
        return 1;
    }

    uint64_t sec = info.uptime_sec;
    uint32_t hours = (uint32_t)(sec / 3600);
    uint32_t mins = (uint32_t)((sec % 3600) / 60);
    uint32_t secs = (uint32_t)(sec % 60);

    printf("up %02d:%02d:%02d, %d processes\n", hours, mins, secs, info.process_count);

    return 0;
}
