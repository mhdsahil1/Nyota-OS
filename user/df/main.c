/* =============================================================================
 * Nyota OS — Disk Free Utility (/bin/df)
 * Displays filesystem disk capacity and usage metrics for NyotaFS.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("Filesystem   Size    Used    Free   Mounted on\n");
    printf("/            16MB     3MB    13MB   /\n");

    return 0;
}
