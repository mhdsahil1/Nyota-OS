/* =============================================================================
 * Nyota OS — Filesystem Sync Utility (/bin/sync)
 * Flushes dirty filesystem buffers and syncs disk storage.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (sync() != 0) {
        printf("sync: failed to synchronize filesystems\n");
        return 1;
    }

    return 0;
}
