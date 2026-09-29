/* =============================================================================
 * Nyota OS — Free Memory Utility (/bin/free)
 * Displays physical memory utilization from kernel page frame allocator.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    sysinfo_data_t info;
    if (sysinfo(&info) != 0) {
        printf("free: failed to query memory allocator\n");
        return 1;
    }

    uint64_t total_mb = info.total_ram / (1024 * 1024);
    uint64_t used_mb = info.used_ram / (1024 * 1024);
    uint64_t free_mb = info.free_ram / (1024 * 1024);

    printf("Memory Statistics:\n");
    printf("Total:      %4d MB\n", (int)total_mb);
    printf("Used:       %4d MB\n", (int)used_mb);
    printf("Free:       %4d MB\n", (int)free_mb);

    return 0;
}
