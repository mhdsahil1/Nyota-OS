/* =============================================================================
 * Nyota OS — User Utility: crash (/bin/crash)
 * Deliberately dereferences invalid memory (0x10) to trigger Ring 3 #PF.
 * Used for verifying exception-to-signal conversion and kernel stability.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* Deliberate invalid memory access */
    volatile int *bad_ptr = (volatile int *)0x10;
    *bad_ptr = 42;

    /* Should not be reached */
    return 0;
}
