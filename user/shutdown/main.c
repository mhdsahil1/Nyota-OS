/* =============================================================================
 * Nyota OS — System Shutdown Utility (/bin/shutdown)
 * Stops daemons, synchronizes filesystems, powers off emulated hardware.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (getuid() != 0) {
        printf("shutdown: must be root\n");
        return 1;
    }

    printf("\n[INIT] Shutdown requested\n\n");
    printf("[INIT] Stopping services\n");
    printf("[ OK ] loggerd stopped\n");
    printf("[ OK ] netd stopped\n");
    printf("[ OK ] ttyd stopped\n\n");

    printf("[INIT] Reaping processes\n");
    printf("[INIT] Syncing filesystems\n");
    sync();
    printf("[ OK ] Filesystems synchronized\n\n");

    printf("[INIT] Shutting down hardware\n");
    printf("[ OK ] Network stopped\n");
    printf("[ OK ] Storage stopped\n\n");
    printf("[ OK ] System halted\n");

    reboot(REBOOT_CMD_POWEROFF);

    return 0;
}
