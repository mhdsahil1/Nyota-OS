/* =============================================================================
 * Nyota OS — Identity Utility (/bin/id)
 * Prints real and effective user and group identities.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    uint32_t u = getuid();
    uint32_t g = getgid();
    uint32_t eu = geteuid();
    uint32_t eg = getegid();

    printf("uid=%u gid=%u euid=%u egid=%u\n", u, g, eu, eg);
    return 0;
}
