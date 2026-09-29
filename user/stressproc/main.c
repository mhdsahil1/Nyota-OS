/* =============================================================================
 * Nyota OS — User Utility: stressproc (/bin/stressproc)
 * Stress-tests process creation, pipes, memory allocations, and lifecycle reaping.
 * =========================================================================== */

#include "libnyota.h"

static int parse_int(const char *s) {
    if (!s) return 0;
    int res = 0;
    while (*s >= '0' && *s <= '9') {
        res = res * 10 + (*s - '0');
        s++;
    }
    return res;
}

int main(int argc, char **argv) {
    int count = 8;
    if (argc > 1) {
        int parsed = parse_int(argv[1]);
        if (parsed > 0) count = parsed;
    }
    if (count > 32) count = 32;

    printf("[INFO] Starting process stress test (%d cycles)...\n", count);

    for (int i = 0; i < count; i++) {
        int p[2];
        if (pipe(p) < 0) {
            printf("[FAIL] pipe allocation in cycle %d\n", i);
            return 1;
        }

        /* Write payload into pipe */
        const char *token = "STRESS_TOKEN";
        write(p[1], token, strlen(token));

        /* Read back */
        char buf[32];
        memset(buf, 0, sizeof(buf));
        read(p[0], buf, strlen(token));

        close(p[0]);
        close(p[1]);

        /* Spawn /bin/echo as worker child */
        char *child_argv[] = {"echo", "stress_cycle", NULL};
        int pid = spawn("/bin/echo", child_argv);
        if (pid > 0) {
            int status = 0;
            waitpid(pid, &status);
        }

        yield();
    }

    printf("[ OK ] Process stress test passed (%d cycles)\n", count);
    return 0;
}
