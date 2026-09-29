/* =============================================================================
 * Nyota OS — System Reboot Utility (/bin/reboot)
 * Initiates clean filesystem synchronization and hardware CPU reset.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (getuid() != 0) {
        printf("reboot: must be root\n");
        return 1;
    }

    printf("[INIT] Rebooting system...\n");
    sync();
    reboot(REBOOT_CMD_REBOOT);

    return 0;
}
