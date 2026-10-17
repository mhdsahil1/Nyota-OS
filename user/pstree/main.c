/* =============================================================================
 * Nyota OS — Process Tree Visualizer (pstree)
 * Displays process hierarchy, states, CPU affinity, context switches, and memory.
 * =========================================================================== */

#include "libnyota.h"

static proc_info_t procs[64];
static int num_procs = 0;
static bool show_details = true;

static void print_indent(int depth, bool is_last) {
    for (int i = 0; i < depth - 1; i++) {
        printf("  │ ");
    }
    if (depth > 0) {
        printf(is_last ? "  └── " : "  ├── ");
    }
}

static void print_process_tree(uint32_t parent_pid, int depth) {
    /* Count children of this parent */
    int child_indices[64];
    int child_count = 0;

    for (int i = 0; i < num_procs; i++) {
        if (procs[i].ppid == parent_pid) {
            child_indices[child_count++] = i;
        }
    }

    for (int c = 0; c < child_count; c++) {
        int idx = child_indices[c];
        bool is_last = (c == child_count - 1);

        print_indent(depth, is_last);

        printf("%s(%u) [%s", procs[idx].name, procs[idx].pid, procs[idx].state);
        if (show_details) {
            printf(", CPU %u, %lu ctxsw, %lu KB",
                   procs[idx].current_cpu,
                   procs[idx].context_switches,
                   procs[idx].memory_bytes / 1024);
        }
        printf("]\n");

        /* Recurse for grandchildren */
        print_process_tree(procs[idx].pid, depth + 1);
    }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    num_procs = getprocs(procs, 64);
    if (num_procs <= 0) {
        printf("pstree: failed to retrieve process table\n");
        return 1;
    }

    printf("=== Nyota OS Process Hierarchy ===\n");

    /* Find root process(es) (typically PID 1 or PPID 0) */
    bool found_root = false;
    for (int i = 0; i < num_procs; i++) {
        if (procs[i].ppid == 0 || procs[i].pid == 1) {
            printf("%s(%u) [%s", procs[i].name, procs[i].pid, procs[i].state);
            if (show_details) {
                printf(", CPU %u, %lu ctxsw, %lu KB",
                       procs[i].current_cpu,
                       procs[i].context_switches,
                       procs[i].memory_bytes / 1024);
            }
            printf("]\n");
            print_process_tree(procs[i].pid, 1);
            found_root = true;
        }
    }

    if (!found_root) {
        /* Fallback: print flat */
        for (int i = 0; i < num_procs; i++) {
            printf("%s(%u) [PPID %u, %s]\n", procs[i].name, procs[i].pid, procs[i].ppid, procs[i].state);
        }
    }

    printf("----------------------------------\n");
    printf("Total processes: %d\n", num_procs);
    return 0;
}
