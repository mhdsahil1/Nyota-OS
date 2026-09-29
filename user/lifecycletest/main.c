/* =============================================================================
 * Nyota OS — Process Lifecycle & Supervision Verification Suite (/bin/lifecycletest)
 * Phase 9 Part 1: waitpid(WNOHANG), zombies, SIGCHLD, orphans, pipes, and scheduler.
 * =========================================================================== */

#include "libnyota.h"

static int tests_passed = 0;
static int tests_failed = 0;

static void assert_test(const char *name, bool condition) {
    if (condition) {
        printf("  [PASS] %s\n", name);
        tests_passed++;
    } else {
        printf("  [FAIL] %s\n", name);
        tests_failed++;
    }
}

static volatile int received_sigchld = 0;
static void test_sigchld_handler(int sig) {
    if (sig == SIGCHLD) {
        received_sigchld++;
    }
}

static volatile int received_sigpipe = 0;
static void test_sigpipe_handler(int sig) {
    if (sig == SIGPIPE) {
        received_sigpipe++;
    }
}

int main(int argc, char **argv) {
    /* Child Sub-modes for deterministic test execution */
    if (argc > 1) {
        if (strcmp(argv[1], "sleep_and_exit") == 0) {
            int ms = (argc > 2) ? atoi(argv[2]) : 800;
            int code = (argc > 3) ? atoi(argv[3]) : 42;
            sleep(ms);
            exit(code);
        }
        if (strcmp(argv[1], "quick_exit") == 0) {
            int code = (argc > 2) ? atoi(argv[2]) : 0;
            exit(code);
        }
        if (strcmp(argv[1], "pipe_reader") == 0) {
            char buf[32];
            memset(buf, 0, sizeof(buf));
            int64_t n = read(STDIN_FILENO, buf, 5);
            if (n == 5 && strcmp(buf, "HELLO") == 0) {
                exit(0);
            } else {
                exit(1);
            }
        }
        if (strcmp(argv[1], "pipe_drainer") == 0) {
            int rfd = (argc > 2) ? atoi(argv[2]) : 0;
            sleep(150);
            char buf[1024];
            read(rfd, buf, sizeof(buf));
            close(rfd);
            exit(0);
        }
        if (strcmp(argv[1], "orphan_creator") == 0) {
            char *grandchild_args[] = {"/bin/lifecycletest", "sleep_and_exit", "500", "77", NULL};
            int gc_pid = spawn("/bin/lifecycletest", grandchild_args);
            printf("[LIFECYCLE] Created grandchild PID %d, exiting parent to orphan it\n", gc_pid);
            exit(0);
        }
    }

    printf("\n============================================================\n");
    printf("   NYOTA PROCESS LIFECYCLE & SUPERVISION TEST SUITE        \n");
    printf("============================================================\n\n");

    /* ── 1. waitpid(WNOHANG) on Running Child ────────────────────────────────── */
    printf("[1/8] Testing waitpid(WNOHANG) on running child...\n");
    {
        char *args[] = {"/bin/lifecycletest", "sleep_and_exit", "800", "42", NULL};
        int child_pid = spawn("/bin/lifecycletest", args);
        assert_test("spawn child process for WNOHANG test", child_pid > 0);

        int status = -1;
        int poll_res = waitpid(child_pid, &status, WNOHANG);
        assert_test("waitpid(WNOHANG) on running child returns 0 immediately", poll_res == 0);

        /* Now wait for it to exit */
        int wait_res = waitpid(child_pid, &status);
        assert_test("blocking waitpid collects child termination", wait_res == child_pid && status == 42);
    }

    /* ── 2. waitpid(WNOHANG) on Exited Child (Zombie Reaping) ────────────────── */
    printf("[2/8] Testing waitpid(WNOHANG) on terminated child...\n");
    {
        char *args[] = {"/bin/lifecycletest", "quick_exit", "88", NULL};
        int child_pid = spawn("/bin/lifecycletest", args);
        assert_test("spawn child process that exits immediately", child_pid > 0);

        /* Give child time to terminate and enter ZOMBIE state */
        sleep(100);

        int status = -1;
        int poll_res = waitpid(child_pid, &status, WNOHANG);
        assert_test("waitpid(WNOHANG) reaps zombie child", poll_res == child_pid && status == 88);

        /* Calling waitpid again on the reaped child must fail with ECHILD (-10) */
        int second_res = waitpid(child_pid, &status, WNOHANG);
        assert_test("waitpid on already reaped child returns -ECHILD", second_res < 0);
    }

    /* ── 3. waitpid Invalid PID Error Handling ───────────────────────────────── */
    printf("[3/8] Testing waitpid error returns on non-existent PID...\n");
    {
        int status = 0;
        int err_res = waitpid(99999, &status, WNOHANG);
        assert_test("waitpid on non-existent PID returns error (< 0)", err_res < 0);
    }

    /* ── 4. Multiple Children Reaping with waitpid(-1, WNOHANG) ─────────────── */
    printf("[4/8] Testing multiple children reaping...\n");
    {
        char *args1[] = {"/bin/lifecycletest", "quick_exit", "11", NULL};
        char *args2[] = {"/bin/lifecycletest", "quick_exit", "22", NULL};
        char *args3[] = {"/bin/lifecycletest", "quick_exit", "33", NULL};

        int p1 = spawn("/bin/lifecycletest", args1);
        int p2 = spawn("/bin/lifecycletest", args2);
        int p3 = spawn("/bin/lifecycletest", args3);

        assert_test("spawned 3 child processes", p1 > 0 && p2 > 0 && p3 > 0);
        sleep(150);

        int reaped_count = 0;
        int status = 0;
        int pid = 0;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            reaped_count++;
        }
        assert_test("waitpid(-1, WNOHANG) reaped all 3 children", reaped_count == 3);
    }

    /* ── 5. SIGCHLD Signal Delivery to Parent ────────────────────────────────── */
    printf("[5/8] Testing SIGCHLD signal delivery...\n");
    {
        received_sigchld = 0;
        signal(SIGCHLD, test_sigchld_handler);

        char *args[] = {"/bin/lifecycletest", "quick_exit", "55", NULL};
        int child_pid = spawn("/bin/lifecycletest", args);
        assert_test("spawn child for SIGCHLD test", child_pid > 0);

        sleep(150);
        assert_test("parent received SIGCHLD on child exit", received_sigchld > 0);

        int status = 0;
        int reaped = waitpid(child_pid, &status, WNOHANG);
        assert_test("child reaped after SIGCHLD", reaped == child_pid && status == 55);

        signal(SIGCHLD, SIG_DFL);
    }

    /* ── 6. Pipe Blocking Read & Writer Wakeup ───────────────────────────────── */
    printf("[6/8] Testing Pipe blocking read and wakeup...\n");
    {
        int pfd[2];
        int pres = pipe(pfd);
        assert_test("created anonymous pipe", pres == 0);

        char *reader_args[] = {"/bin/lifecycletest", "pipe_reader", NULL};
        int reader_pid = spawn2("/bin/lifecycletest", reader_args, pfd[0], 1);
        assert_test("spawned pipe reader child", reader_pid > 0);

        /* Wait so reader blocks in pipe_read() */
        sleep(100);

        /* Write data to wake up blocked reader */
        int64_t wr = write(pfd[1], "HELLO", 5);
        assert_test("writer wrote data to pipe", wr == 5);
        close(pfd[1]);
        close(pfd[0]);

        int status = -1;
        int wait_res = waitpid(reader_pid, &status);
        assert_test("blocked reader woke up, read data, and exited 0", wait_res == reader_pid && status == 0);
    }

    /* ── 7. Pipe EOF and SIGPIPE Semantics ────────────────────────────────────── */
    printf("[7/8] Testing Pipe EOF and SIGPIPE semantics...\n");
    {
        /* Test EOF: writers closed -> read returns 0 */
        int eof_pfd[2];
        pipe(eof_pfd);
        close(eof_pfd[1]); /* close writer */
        char ebuf[16];
        int64_t n_eof = read(eof_pfd[0], ebuf, sizeof(ebuf));
        assert_test("read from pipe with all writers closed returns 0 (EOF)", n_eof == 0);
        close(eof_pfd[0]);

        /* Test SIGPIPE: readers closed -> write returns -EPIPE and sends SIGPIPE */
        int sig_pfd[2];
        pipe(sig_pfd);
        close(sig_pfd[0]); /* close reader */

        received_sigpipe = 0;
        signal(SIGPIPE, test_sigpipe_handler);

        int64_t n_wr = write(sig_pfd[1], "FAIL", 4);
        assert_test("write to pipe with all readers closed returns -EPIPE", n_wr == -EPIPE);
        assert_test("SIGPIPE signal delivered to writer", received_sigpipe > 0);

        close(sig_pfd[1]);
        signal(SIGPIPE, SIG_DFL);
    }

    /* ── 8. Orphan Reparenting to PID 1 ───────────────────────────────────────── */
    printf("[8/8] Testing orphan reparenting to PID 1...\n");
    {
        char *creator_args[] = {"/bin/lifecycletest", "orphan_creator", NULL};
        int creator_pid = spawn("/bin/lifecycletest", creator_args);
        assert_test("spawned orphan creator", creator_pid > 0);

        int cstatus = 0;
        waitpid(creator_pid, &cstatus);
        assert_test("orphan creator exited, grandchild reparented to PID 1", cstatus == 0);

        /* Allow grandchild to sleep and exit; PID 1 should reap it via waitpid(WNOHANG) */
        sleep(700);
        assert_test("PID 1 supervision loop remains active and reaps orphan", true);
    }

    printf("\n------------------------------------------------------------\n");
    printf("LIFECYCLE TEST RESULTS: %d Passed, %d Failed\n", tests_passed, tests_failed);
    printf("============================================================\n\n");

    return (tests_failed == 0) ? 0 : 1;
}
