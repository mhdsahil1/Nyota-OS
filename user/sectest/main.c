/* =============================================================================
 * Nyota OS — Phase 14 Security Hardening Verification Suite (/bin/sectest)
 * Verifies kernel user/kernel isolation, credentials, file permissions,
 * capability enforcement, safe user pointers, and syscall filtering.
 * =========================================================================== */

#include "libnyota.h"

static int total_tests = 0;
static int passed_tests = 0;

static void assert_test(const char *name, bool condition) {
    total_tests++;
    if (condition) {
        passed_tests++;
        printf("  [ PASS ] %s\n", name);
    } else {
        printf("  [ FAIL ] %s\n", name);
    }
}

static int run_sandbox_child(void) {
    /* Filter: Deny SYS_MKDIR (11) */
    uint64_t mask0 = (1ULL << 11);
    uint64_t mask1 = 0;
    int r = set_syscall_filter(SYSCALL_FILTER_DENY, mask0, mask1);
    if (r != 0) return 1;

    /* Verify SYS_MKDIR is now blocked by kernel */
    int mr = mkdir("/tmp/sandbox_test", 0755);
    if (mr >= 0) return 2; /* Should have failed */

    /* Verify permitted syscalls (getpid, yield) still work */
    int pid = getpid();
    if (pid <= 0) return 3;

    /* Verify filter cannot be cleared once locked */
    int r_unlock = set_syscall_filter(SYSCALL_FILTER_OFF, 0, 0);
    if (r_unlock == 0) return 4; /* Should have failed because locked */

    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--sandbox-child") == 0) {
        return run_sandbox_child();
    }

    printf("============================================================\n");
    printf("         NYOTA OS PHASE 14 SECURITY VERIFICATION           \n");
    printf("============================================================\n\n");

    /* 1. Identity & Credentials */
    printf("[1/7] Testing Process Credentials & Identity Model...\n");
    uint32_t u = getuid();
    uint32_t eu = geteuid();
    uint32_t g = getgid();
    uint32_t eg = getegid();
    assert_test("identity_uid_is_1000", u == 1000);
    assert_test("identity_euid_is_1000", eu == 1000);
    assert_test("identity_gid_is_1000", g == 1000);
    assert_test("identity_egid_is_1000", eg == 1000);

    /* 2. Unauthorized Privilege Escalation */
    printf("\n[2/7] Testing Privilege Escalation Prevention...\n");
    int r_seteuid = seteuid(0);
    assert_test("unauthorized_seteuid_rejected", r_seteuid < 0);
    int r_setuid = setuid(0);
    assert_test("unauthorized_setuid_rejected", r_setuid < 0);
    int r_setegid = setegid(0);
    assert_test("unauthorized_setegid_rejected", r_setegid < 0);
    int r_setgid = setgid(0);
    assert_test("unauthorized_setgid_rejected", r_setgid < 0);

    /* 3. File Permissions & Access Control */
    printf("\n[3/7] Testing File Permission Enforcement...\n");
    int fd_shadow = open("/etc/shadow", O_RDONLY);
    assert_test("shadow_file_read_blocked_mode_0600", fd_shadow < 0);
    if (fd_shadow >= 0) close(fd_shadow);

    int r_chmod_sys = chmod("/etc/passwd", 0777);
    assert_test("unauthorized_chmod_system_file_blocked", r_chmod_sys < 0);

    int r_chown_sys = chown("/etc/passwd", 1000, 1000);
    assert_test("unauthorized_chown_system_file_blocked", r_chown_sys < 0);

    /* 4. Owner Permission & chmod */
    printf("\n[4/7] Testing User File Ownership & Permissions...\n");
    int fd_own = open("/home/sahil/sec_test.tmp", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    assert_test("user_can_create_own_file", fd_own >= 0);
    if (fd_own >= 0) {
        write(fd_own, "nyota", 5);
        close(fd_own);
    }
    int r_chmod_own = chmod("/home/sahil/sec_test.tmp", 0600);
    assert_test("owner_can_chmod_own_file", r_chmod_own == 0);

    /* 5. Capability Enforcement */
    printf("\n[5/7] Testing Capability-Based Security Boundaries...\n");
    audit_event_t evs[4];
    int64_t r_audit = audit_log_op(AUDIT_CMD_READ, evs, 4);
    assert_test("auditlog_read_denied_without_cap", r_audit < 0);

    /* 6. Safe User Pointer & Boundary Validation */
    printf("\n[6/7] Testing Syscall Pointer & Overflow Hardening...\n");
    int64_t r_kptr = write(1, (const void *)0xFFFFFFFF80000000ULL, 16);
    assert_test("kernel_pointer_in_syscall_returns_efault", r_kptr < 0);

    int64_t r_uptr = write(1, (const void *)0x00007FFFF0000000ULL, 16);
    assert_test("unmapped_pointer_in_syscall_returns_efault", r_uptr < 0);

    int64_t r_null = read(0, NULL, 16);
    assert_test("null_pointer_in_syscall_returns_efault", r_null < 0);

    int64_t r_ovf = read(0, (void *)0x8000000000ULL, 0xFFFFFFFFFFFFFFFFULL);
    assert_test("integer_overflow_length_rejected", r_ovf < 0);

    /* 7. Syscall Filtering Sandbox */
    printf("\n[7/7] Testing Syscall Sandbox Filter...\n");
    char *cargv[] = {"/bin/sectest", "--sandbox-child", NULL};
    int child_pid = spawn("/bin/sectest", cargv);
    int status = -1;
    if (child_pid > 0) {
        waitpid(child_pid, &status);
    }
    assert_test("syscall_filtering_sandbox_enforced", status == 0);

    printf("\n============================================================\n");
    printf("     PHASE 14 SECURITY TESTS: %d / %d PASSED (%s)\n",
           passed_tests, total_tests,
           (passed_tests == total_tests) ? "100% SUCCESS" : "FAILURES DETECTED");
    printf("============================================================\n\n");

    return (passed_tests == total_tests) ? 0 : 1;
}
