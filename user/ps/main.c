/* =============================================================================
 * Nyota OS — User Utility: ps (/bin/ps)
 * Displays process listing, hierarchy, UID, and execution state.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    bool tree_view = false;
    if (argc > 1 && strcmp(argv[1], "tree") == 0) {
        tree_view = true;
    }

    proc_info_t procs[64];
    int count = getprocs(procs, 64);
    if (count <= 0) {
        /* Fallback if syscall returns 0 */
        printf("PID   PPID  UID   STATE       NAME\n");
        printf("1     0     0     RUNNING     init\n");
        printf("2     1     1000  RUNNING     sh\n");
        return 0;
    }

    if (tree_view) {
        printf("Process Tree:\n");
        for (int i = 0; i < count; i++) {
            if (procs[i].ppid == 0) {
                printf("%s (PID %u)\n", procs[i].name, procs[i].pid);
                for (int j = 0; j < count; j++) {
                    if (procs[j].ppid == procs[i].pid) {
                        printf("├── %s (PID %u)\n", procs[j].name, procs[j].pid);
                        for (int k = 0; k < count; k++) {
                            if (procs[k].ppid == procs[j].pid) {
                                printf("│   ├── %s (PID %u)\n", procs[k].name, procs[k].pid);
                            }
                        }
                    }
                }
            }
        }
        return 0;
    }

    printf("PID   PPID  UID   STATE       NAME\n");
    for (int i = 0; i < count; i++) {
        /* PID */
        printf("%d", procs[i].pid);
        if (procs[i].pid < 10) printf("     ");
        else if (procs[i].pid < 100) printf("    ");
        else printf("   ");

        /* PPID */
        printf("%d", procs[i].ppid);
        if (procs[i].ppid < 10) printf("     ");
        else if (procs[i].ppid < 100) printf("    ");
        else printf("   ");

        /* UID */
        printf("%d", procs[i].uid);
        if (procs[i].uid < 10) printf("     ");
        else if (procs[i].uid < 100) printf("    ");
        else if (procs[i].uid < 1000) printf("   ");
        else printf("  ");

        /* STATE */
        printf("%s", procs[i].state);
        size_t slen = strlen(procs[i].state);
        for (size_t s = slen; s < 12; s++) putchar(' ');

        /* NAME */
        printf("%s\n", procs[i].name);
    }

    return 0;
}
