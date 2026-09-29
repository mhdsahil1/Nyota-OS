/* =============================================================================
 * Nyota OS — User Utility: secinfo (/bin/secinfo)
 * Displays kernel security features and isolation architecture diagnostics.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    secinfo_t info;
    memset(&info, 0, sizeof(info));

    if (secinfo(&info) < 0) {
        printf("secinfo: failed to query kernel security status\n");
        return 1;
    }

    printf("Kernel security features\n");
    printf("------------------------\n");
    printf("User/kernel isolation : %s\n", info.isolation_enabled ? "enabled" : "disabled");
    printf("Guard pages           : %s\n", info.guard_pages_enabled ? "enabled" : "disabled");
    printf("Pointer validation    : %s\n", info.pointer_validation_enabled ? "enabled" : "disabled");
    printf("Capabilities          : %s\n", info.capabilities_enabled ? "enabled" : "disabled");
    printf("Resource limits       : %s\n", info.resource_limits_enabled ? "enabled" : "disabled");
    printf("ASLR foundation       : %s\n", info.aslr_enabled ? "enabled" : "disabled");
    printf("\nActive IPC & Process Diagnostics:\n");
    printf("  Processes running   : %u\n", (unsigned int)info.active_processes);
    printf("  Active pipes        : %u\n", (unsigned int)info.active_pipes);
    printf("  Shared memory segs  : %u\n", (unsigned int)info.active_shm_segments);

    return 0;
}
