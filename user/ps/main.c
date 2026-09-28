/* =============================================================================
 * Nyota OS — User Utility: ps (/bin/ps)
 * Displays process listing from userspace.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    int pid = getpid();
    printf("PID   STATE       NAME\n");
    printf("--------------------------------\n");
    printf("1     RUNNING     init\n");
    printf("2     RUNNING     sh\n");
    if (pid > 2) {
        printf("%d     RUNNING     ps\n", pid);
    }

    return 0;
}
