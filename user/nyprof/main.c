/* =============================================================================
 * Nyota OS — Performance Profiler (nyprof)
 * Measures syscall latencies, call counts, and execution metrics.
 * =========================================================================== */

#include "libnyota.h"

static void print_profiler_report(const prof_report_t *rep) {
    printf("===================================================================\n");
    printf("                 NYOTA SYSCALL PERFORMANCE PROFILE                \n");
    printf("===================================================================\n");
    printf("%-20s  %-8s  %-12s  %-10s  %-10s\n",
           "SYSCALL", "CALLS", "AVG (CYCLES)", "MIN", "MAX");
    printf("-------------------------------------------------------------------\n");

    int active_syscalls = 0;
    for (int i = 0; i < 128; i++) {
        if (rep->stats[i].call_count > 0) {
            const char *name = syscall_name_lookup((uint32_t)i);
            printf("%-20s  %-8lu  %-12lu  %-10lu  %-10lu\n",
                   name,
                   rep->stats[i].call_count,
                   rep->stats[i].avg_cycles,
                   rep->stats[i].min_cycles,
                   rep->stats[i].max_cycles);
            active_syscalls++;
        }
    }

    if (active_syscalls == 0) {
        printf("  (no syscalls recorded during profiling session)\n");
    }

    printf("===================================================================\n");
    printf("Total syscall invocations: %lu\n", rep->total_syscalls);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: nyprof <command> [args...]\n");
        printf("       nyprof -s   (display active profiler report)\n");
        printf("       nyprof -r   (reset profiler statistics)\n");
        return 1;
    }

    if (strcmp(argv[1], "-s") == 0 || strcmp(argv[1], "--status") == 0) {
        prof_report_t rep;
        memset(&rep, 0, sizeof(rep));
        if (prof_op(PROF_OP_GET_STATS, &rep) < 0) {
            printf("nyprof: failed to retrieve profiling statistics\n");
            return 1;
        }
        print_profiler_report(&rep);
        return 0;
    }

    if (strcmp(argv[1], "-r") == 0 || strcmp(argv[1], "--reset") == 0) {
        prof_op(PROF_OP_RESET, NULL);
        printf("nyprof: profiler statistics reset.\n");
        return 0;
    }

    /* Profiling a specific command */
    prof_op(PROF_OP_START, NULL);

    char exec_path[128];
    if (argv[1][0] == '/') {
        strncpy(exec_path, argv[1], sizeof(exec_path) - 1);
        exec_path[sizeof(exec_path) - 1] = '\0';
    } else {
        snprintf(exec_path, sizeof(exec_path), "/bin/%s", argv[1]);
    }

    char *child_argv[32];
    int child_argc = 0;
    for (int i = 1; i < argc && child_argc < 31; i++) {
        child_argv[child_argc++] = argv[i];
    }
    child_argv[child_argc] = NULL;

    int child_pid = spawn(exec_path, child_argv);
    if (child_pid <= 0) {
        printf("nyprof: failed to spawn %s\n", exec_path);
        prof_op(PROF_OP_STOP, NULL);
        return 1;
    }

    int status = 0;
    waitpid(child_pid, &status);

    prof_op(PROF_OP_STOP, NULL);

    prof_report_t rep;
    memset(&rep, 0, sizeof(rep));
    prof_op(PROF_OP_GET_STATS, &rep);

    printf("\nnyprof: command '%s' completed with exit status %d\n", argv[1], status);
    print_profiler_report(&rep);
    return 0;
}
