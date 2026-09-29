/* =============================================================================
 * Nyota OS — System Information Utility (/bin/sysinfo)
 * Comprehensive system overview: kernel, architecture, memory, processes, uptime.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    sysinfo_data_t info;
    if (sysinfo(&info) != 0) {
        printf("sysinfo: failed to retrieve system statistics\n");
        return 1;
    }

    uint64_t total_mb = info.total_ram / (1024 * 1024);
    uint64_t used_mb = info.used_ram / (1024 * 1024);
    uint64_t free_mb = info.free_ram / (1024 * 1024);

    uint64_t sec = info.uptime_sec;
    uint32_t hours = (uint32_t)(sec / 3600);
    uint32_t mins = (uint32_t)((sec % 3600) / 60);
    uint32_t secs = (uint32_t)(sec % 60);

    printf("========================================\n");
    printf("         NYOTA OS SYSTEM INFO           \n");
    printf("========================================\n");
    printf("OS Kernel    : NyotaOS %s\n", info.kernel_ver);
    printf("Architecture : %s\n", info.machine);
    printf("Memory       : Total %d MB | Used %d MB | Free %d MB\n", (int)total_mb, (int)used_mb, (int)free_mb);
    printf("Processes    : %d active\n", info.process_count);
    printf("Uptime       : %02d:%02d:%02d\n", hours, mins, secs);
    printf("Filesystem   : NyotaFS mounted on /\n");
    printf("Networking   : Intel E1000 Ethernet (10.0.2.15)\n");
    printf("========================================\n");

    return 0;
}
