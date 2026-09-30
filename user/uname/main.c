/* =============================================================================
 * Nyota OS — System Name Utility (/bin/uname)
 * Displays operating system, release version, and hardware architecture.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    bool all = false;
    bool sysname = false;
    bool release = false;
    bool machine = false;

    if (argc == 1) {
        sysname = true;
    } else {
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-a") == 0) all = true;
            else if (strcmp(argv[i], "-s") == 0) sysname = true;
            else if (strcmp(argv[i], "-r") == 0) release = true;
            else if (strcmp(argv[i], "-m") == 0) machine = true;
        }
    }

    if (all) {
        printf("NyotaOS nyota 1.0.0 x86_64\n");
    } else {
        bool first = true;
        if (sysname) {
            printf("NyotaOS");
            first = false;
        }
        if (release) {
            if (!first) printf(" ");
            printf("1.0.0");
            first = false;
        }
        if (machine) {
            if (!first) printf(" ");
            printf("x86_64");
            first = false;
        }
        printf("\n");
    }

    return 0;
}
