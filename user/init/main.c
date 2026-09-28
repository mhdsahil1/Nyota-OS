/* =============================================================================
 * Nyota OS — Userspace Init Process (/init)
 * First process executed in Ring 3 (PID 1). Launches and supervises the shell.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("\n");
    printf("========================================\n");
    printf("          NYOTA OS INIT (PID 1)         \n");
    printf("========================================\n");
    printf("[INIT] System initialization complete.\n");

    /* Run automated verification suite on startup */
    printf("[INIT] Running automated test suite (/bin/test)...\n");
    char *test_argv[] = {"/bin/test", NULL};
    int test_pid = spawn("/bin/test", test_argv);
    if (test_pid > 0) {
        int test_status = 0;
        waitpid(test_pid, &test_status);
        printf("[INIT] Test suite finished (exit status %d).\n\n", test_status);
    }

    printf("[INIT] Starting userspace interactive shell (/bin/sh)...\n\n");

    while (1) {
        char *sh_argv[] = {"/bin/sh", NULL};
        int pid = spawn("/bin/sh", sh_argv);

        if (pid < 0) {
            printf("[INIT] Failed to spawn /bin/sh (ret=%d). Retrying in 2 seconds...\n", pid);
            sleep(2000);
            continue;
        }

        int status = 0;
        waitpid(pid, &status);

        printf("\n[INIT] Shell process PID %d exited with status %d. Restarting...\n", pid, status);
        sleep(500);
    }

    return 0;
}
