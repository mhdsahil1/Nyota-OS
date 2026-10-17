/* =============================================================================
 * Nyota OS — System-Call Tracer (nytrace)
 * Traces syscalls, arguments, return values, and execution latency.
 * =========================================================================== */

#include "libnyota.h"

static void print_syscall_event(const trace_event_t *ev) {
    const char *name = syscall_name_lookup(ev->syscall_num);

    printf("[%u] %s(", ev->pid, name);

    /* Format arguments compactly depending on syscall type */
    if (ev->syscall_num == 0 /* write */ || ev->syscall_num == 7 /* read */) {
        printf("fd=%u, buf=0x%lx, len=%lu", (uint32_t)ev->args[0], ev->args[1], ev->args[2]);
    } else if (ev->syscall_num == 5 /* open */) {
        printf("path=0x%lx, flags=0x%x, mode=0x%x", ev->args[0], (uint32_t)ev->args[1], (uint32_t)ev->args[2]);
    } else if (ev->syscall_num == 6 /* close */) {
        printf("fd=%u", (uint32_t)ev->args[0]);
    } else if (ev->syscall_num == 14 /* spawn */ || ev->syscall_num == 13 /* exec */) {
        printf("path=0x%lx, argv=0x%lx", ev->args[0], ev->args[1]);
    } else if (ev->syscall_num == 15 /* waitpid */) {
        printf("pid=%d, status=0x%lx", (int)ev->args[0], ev->args[1]);
    } else {
        printf("0x%lx, 0x%lx, 0x%lx", ev->args[0], ev->args[1], ev->args[2]);
    }

    printf(") = %ld [%lu us]\n", ev->ret_val, ev->duration_us);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: nytrace <command> [args...]\n");
        printf("       nytrace -p <pid>\n");
        return 1;
    }

    if (strcmp(argv[1], "-p") == 0) {
        if (argc < 3) {
            printf("nytrace: missing PID for -p\n");
            return 1;
        }
        int pid = atoi(argv[2]);
        if (pid <= 0) {
            printf("nytrace: invalid PID %s\n", argv[2]);
            return 1;
        }

        if (trace_op(TRACE_OP_ATTACH, (uint32_t)pid, NULL) < 0) {
            printf("nytrace: cannot attach to process %d (permission denied or protected)\n", pid);
            return 1;
        }

        printf("nytrace: attached to process %d. Press Ctrl+C or wait...\n", pid);

        trace_event_t events[32];
        for (int iter = 0; iter < 10; iter++) {
            int n = (int)trace_op(TRACE_OP_READ, 0, events);
            for (int i = 0; i < n; i++) {
                print_syscall_event(&events[i]);
            }
            sleep(100);
        }

        trace_op(TRACE_OP_DETACH, (uint32_t)pid, NULL);
        printf("nytrace: detached from process %d\n", pid);
        return 0;
    }

    /* Command invocation mode: nytrace <cmd> [args...] */
    trace_op(TRACE_OP_CLEAR, 0, NULL);
    trace_op(TRACE_OP_ENABLE_SELF, 0, NULL);

    /* Construct path if relative */
    char exec_path[128];
    if (argv[1][0] == '/') {
        strncpy(exec_path, argv[1], sizeof(exec_path) - 1);
        exec_path[sizeof(exec_path) - 1] = '\0';
    } else {
        snprintf(exec_path, sizeof(exec_path), "/bin/%s", argv[1]);
    }

    /* Build argument array */
    char *child_argv[32];
    int child_argc = 0;
    for (int i = 1; i < argc && child_argc < 31; i++) {
        child_argv[child_argc++] = argv[i];
    }
    child_argv[child_argc] = NULL;

    int child_pid = spawn(exec_path, child_argv);
    if (child_pid <= 0) {
        printf("nytrace: failed to execute %s\n", exec_path);
        trace_op(TRACE_OP_DETACH, 0, NULL);
        return 1;
    }

    int status = 0;
    waitpid(child_pid, &status);

    /* Read and display recorded trace events */
    trace_event_t events[32];
    int total_events = 0;
    uint64_t total_us = 0;

    int n;
    while ((n = (int)trace_op(TRACE_OP_READ, 0, events)) > 0) {
        for (int i = 0; i < n; i++) {
            print_syscall_event(&events[i]);
            total_events++;
            total_us += events[i].duration_us;
        }
    }

    trace_op(TRACE_OP_DETACH, 0, NULL);

    printf("nytrace: process %d exited with status %d (traced %d syscalls, %lu us total)\n",
           child_pid, status, total_events, total_us);
    return 0;
}
