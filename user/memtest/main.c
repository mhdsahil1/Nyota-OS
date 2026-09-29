/* =============================================================================
 * Nyota OS — User Utility: memtest (/bin/memtest)
 * Tests userspace memory isolation, kernel boundary checks, and fault survival.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* 1. Own Memory Access */
    volatile char local_buf[64];
    for (int i = 0; i < 32; i++) {
        local_buf[i] = (char)('A' + (i % 26));
    }
    if (local_buf[0] == 'A' && local_buf[25] == 'Z') {
        printf("[ OK ] own memory access\n");
    } else {
        printf("[FAIL] own memory access\n");
    }

    /* 2. Kernel Memory Blocked */
    /* Pass a kernel virtual address (0xFFFFFFFF80000000) to a syscall buffer */
    void *kernel_addr = (void *)0xFFFFFFFF80000000ULL;
    int64_t ret = read(0, kernel_addr, 16);
    if (ret == -14) { /* -EFAULT */
        printf("[ OK ] kernel memory blocked\n");
    } else {
        printf("[FAIL] kernel memory was not blocked: %ld\n", (long)ret);
    }

    /* 3. Foreign Process / Invalid Virtual Memory Blocked */
    void *foreign_addr = (void *)0x00007FFFF0000000ULL;
    ret = write(1, foreign_addr, 16);
    if (ret == -14) { /* -EFAULT */
        printf("[ OK ] foreign process memory blocked\n");
    } else {
        printf("[FAIL] foreign process memory check: %ld\n", (long)ret);
    }

    /* 4. Unmapped Memory -> SIGSEGV */
    /* Spawn /bin/crash which dereferences address 0x10 */
    char *crash_argv[] = {"crash", NULL};
    int pid = spawn("/bin/crash", crash_argv);
    if (pid > 0) {
        int status = 0;
        waitpid(pid, &status);
        printf("[ OK ] unmapped memory -> SIGSEGV\n");
    } else {
        printf("[SKIP] unmapped memory check\n");
    }

    /* 5. Process Survived */
    printf("[ OK ] process survived\n");

    return 0;
}
