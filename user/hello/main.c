/* =============================================================================
 * Nyota OS — User Utility: hello (/bin/hello)
 * Demonstrates basic userspace execution from filesystem and standard output.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    puts("Hello from Nyota userspace!");
    return 0;
}
