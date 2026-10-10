/* =============================================================================
 * Nyota OS — Whoami Utility (/bin/whoami)
 * Prints the current effective user name.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    uint32_t eu = geteuid();
    if (eu == 0) {
        printf("root\n");
        return 0;
    }

    const char *env_user = getenv("USER");
    if (env_user && env_user[0]) {
        printf("%s\n", env_user);
    } else {
        printf("uid%u\n", eu);
    }
    return 0;
}
