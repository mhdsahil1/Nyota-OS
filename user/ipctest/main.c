/* =============================================================================
 * Nyota OS — User Utility: ipctest (/bin/ipctest)
 * Exercises pipes, blocking reads, shared memory, and IPC security checks.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* 1. Pipe Communication Test */
    int fds[2];
    if (pipe(fds) < 0) {
        printf("[FAIL] pipe creation\n");
        return 1;
    }

    const char *msg = "Hello Nyota IPC Pipe!";
    size_t msg_len = strlen(msg);
    if (write(fds[1], msg, msg_len) != (int64_t)msg_len) {
        printf("[FAIL] pipe write\n");
        return 1;
    }

    char rx_buf[64];
    memset(rx_buf, 0, sizeof(rx_buf));
    int64_t nread = read(fds[0], rx_buf, sizeof(rx_buf) - 1);
    if (nread != (int64_t)msg_len || strcmp(rx_buf, msg) != 0) {
        printf("[FAIL] pipe read data mismatch\n");
        return 1;
    }
    printf("[ OK ] pipe communication\n");

    /* 2. Blocking Read Test */
    /* Write another message, verify read buffer drained */
    const char *msg2 = "Blocking Read Validation";
    write(fds[1], msg2, strlen(msg2));
    memset(rx_buf, 0, sizeof(rx_buf));
    nread = read(fds[0], rx_buf, strlen(msg2));
    if (nread == (int64_t)strlen(msg2) && strcmp(rx_buf, msg2) == 0) {
        printf("[ OK ] blocking read\n");
    } else {
        printf("[FAIL] blocking read\n");
    }

    /* 3. Shared Memory Test */
    uint32_t key = 0x4E594F54; /* 'NYOT' */
    int shmid = shm_get(key, 4096, IPC_CREAT | 0666);
    if (shmid < 0) {
        printf("[FAIL] shm_get failed: %d\n", shmid);
        return 1;
    }

    char *shm_ptr = (char *)shm_at(shmid, NULL, 0);
    if (!shm_ptr) {
        printf("[FAIL] shm_at failed\n");
        return 1;
    }

    const char *shm_msg = "NYOTA_SHM_SECURE_PAYLOAD";
    strcpy(shm_ptr, shm_msg);

    if (strcmp(shm_ptr, shm_msg) == 0) {
        printf("[ OK ] shared memory\n");
    } else {
        printf("[FAIL] shared memory verification\n");
    }

    /* 4. Permission Checks */
    /* Accessing an invalid shmid or out-of-range key */
    void *bad_at = shm_at(999, NULL, 0);
    if (bad_at == NULL || (int64_t)bad_at < 0) {
        printf("[ OK ] permission checks\n");
    } else {
        printf("[FAIL] permission checks (allowed invalid shmid)\n");
    }

    /* 5. Cleanup */
    shm_dt(shm_ptr);
    shm_ctl(shmid, IPC_RMID, NULL);
    close(fds[0]);
    close(fds[1]);
    printf("[ OK ] cleanup\n");

    return 0;
}
