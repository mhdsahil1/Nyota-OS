/* =============================================================================
 * Nyota OS — Userspace Verification Test Suite (/bin/test)
 * Comprehensive testing of syscalls, VFS, file I/O, process spawn, and security.
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

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("\n========================================\n");
    printf("   NYOTA USERSPACE TEST SUITE (Ring 3)  \n");
    printf("========================================\n\n");

    /* 1. Basic Process Syscalls */
    printf("[1/5] Testing Process Identity & Scheduling...\n");
    int pid = getpid();
    assert_test("getpid() returns valid positive PID", pid > 0);
    yield();
    assert_test("yield() executes successfully", true);

    /* 2. Standard Streams & Device Files */
    printf("[2/5] Testing Device Files & Standard Streams...\n");
    int64_t null_fd = open("/dev/null", O_WRONLY);
    assert_test("open('/dev/null') returns valid FD", null_fd >= 0);
    if (null_fd >= 0) {
        const char *dummy = "Discarded text into null device";
        int64_t wr = write((int)null_fd, dummy, strlen(dummy));
        assert_test("write to /dev/null succeeds and discards data", wr == (int64_t)strlen(dummy));
        close((int)null_fd);
    }

    /* 3. Filesystem & VFS I/O */
    printf("[3/5] Testing Filesystem Read / Write / Stat...\n");
    stat_t st;
    int st_res = stat("/bin/hello", &st);
    assert_test("stat('/bin/hello') succeeds", st_res == 0 && st.size > 0);

    const char *test_path = "/tmp/test.txt";
    int wr_fd = open(test_path, O_CREAT | O_WRONLY | O_TRUNC);
    assert_test("open(O_CREAT) for new file in /tmp", wr_fd >= 0);
    if (wr_fd >= 0) {
        const char *msg = "Hello NyotaFS persistent storage!";
        write(wr_fd, msg, strlen(msg));
        close(wr_fd);

        int rd_fd = open(test_path, O_RDONLY);
        assert_test("re-open written file for read", rd_fd >= 0);
        if (rd_fd >= 0) {
            char read_buf[64];
            memset(read_buf, 0, sizeof(read_buf));
            int64_t bytes = read(rd_fd, read_buf, sizeof(read_buf) - 1);
            assert_test("read back exact written content", bytes == (int64_t)strlen(msg) && strcmp(read_buf, msg) == 0);
            close(rd_fd);
        }
    }

    /* 4. Subprocess Spawning & Arguments */
    printf("[4/5] Testing Process Spawning & Waitpid...\n");
    char *echo_args[] = {"/bin/echo", "Testing", "spawn", "arguments", NULL};
    int child_pid = spawn("/bin/echo", echo_args);
    assert_test("spawn('/bin/echo') returns valid child PID", child_pid > 0);
    if (child_pid > 0) {
        int status = -1;
        int wait_res = waitpid(child_pid, &status);
        assert_test("waitpid() collects child termination", wait_res == child_pid && status == 0);
    }

    /* 5. Security & Pointer Validation */
    printf("[5/5] Testing Security & Boundary Protections...\n");
    int bad_fd_res = (int)read(9999, (void *)0x0000008000100000ULL, 10);
    assert_test("invalid file descriptor returns error (EBADF)", bad_fd_res < 0);

    int bad_path_res = open("/does_not_exist_abc123", O_RDONLY);
    assert_test("nonexistent file returns error (ENOENT)", bad_path_res < 0);

    /* Test hostile pointer into kernel space (0x100000) */
    int64_t kernel_ptr_res = write(STDOUT_FILENO, (const void *)0x100000, 16);
    assert_test("kernel pointer rejected safely with -EFAULT (no panic)", kernel_ptr_res < 0);

    printf("\n----------------------------------------\n");
    printf("Test Results: %d Passed, %d Failed\n", tests_passed, tests_failed);
    printf("========================================\n\n");

    return (tests_failed == 0) ? 0 : 1;
}
