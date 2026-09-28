/* =============================================================================
 * Nyota OS — User Utility: echo (/bin/echo)
 * Demonstrates argc / argv command-line arguments on the user stack.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        printf("%s%s", argv[i], (i == argc - 1) ? "" : " ");
    }
    putchar('\n');
    return 0;
}
