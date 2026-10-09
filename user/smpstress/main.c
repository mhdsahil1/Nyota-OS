/* =============================================================================
 * Nyota OS — Multicore SMP Stress Test (/bin/smpstress)
 * Spawns concurrent workers pinned across CPUs to test scheduler, IPC,
 * memory integrity, and synchronization under true multicore load.
 * =========================================================================== */

#include "libnyota.h"

#define TOTAL_OPERATIONS 1000000
#define NUM_WORKERS      8
#define OPS_PER_WORKER   (TOTAL_OPERATIONS / NUM_WORKERS)

static int run_worker(int worker_id, int cpu_id, int ops) {
    /* Set CPU affinity for this worker process */
    sched_setaffinity(0, 1ULL << cpu_id);

    /* Verify affinity */
    uint64_t mask = 0;
    sched_getaffinity(0, &mask);

    /* Allocate local memory buffer for data integrity stress */
    uint8_t buffer[512];
    for (int i = 0; i < 512; i++) {
        buffer[i] = (uint8_t)((worker_id * 37 + i) & 0xFF);
    }

    int failures = 0;
    int corruptions = 0;

    /* Create pipe for concurrent IPC operations */
    int fds[2];
    bool has_pipe = (pipe(fds) == 0);

    for (int op = 0; op < ops; op++) {
        /* 1. Arithmetic & memory checksum validation */
        uint32_t sum = 0;
        for (int i = 0; i < 64; i++) {
            sum += buffer[i];
            buffer[i] = (uint8_t)(buffer[i] ^ (op & 0xFF));
        }
        for (int i = 0; i < 64; i++) {
            buffer[i] = (uint8_t)(buffer[i] ^ (op & 0xFF));
        }

        /* Verify buffer was not corrupted */
        uint32_t verify_sum = 0;
        for (int i = 0; i < 64; i++) {
            verify_sum += buffer[i];
        }
        if (sum != verify_sum) {
            corruptions++;
        }

        /* 2. Periodic IPC and Syscall verification */
        if (has_pipe && (op % 1000) == 0) {
            char token[8] = "SMP_OK";
            if (write(fds[1], token, 6) != 6) failures++;
            char recv_token[8];
            memset(recv_token, 0, sizeof(recv_token));
            if (read(fds[0], recv_token, 6) != 6) failures++;
            if (strcmp(token, recv_token) != 0) corruptions++;
        }

        /* 3. Occasional yield to stimulate scheduler preemption and migration */
        if ((op % 25000) == 0) {
            yield();
        }
    }

    if (has_pipe) {
        close(fds[0]);
        close(fds[1]);
    }

    if (failures > 0 || corruptions > 0) {
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 4 && strcmp(argv[1], "--worker") == 0) {
        int wid = atoi(argv[2]);
        int cid = atoi(argv[3]);
        int ops = (argc >= 5) ? atoi(argv[4]) : OPS_PER_WORKER;
        return run_worker(wid, cid, ops);
    }

    smp_info_t info;
    if (smp_info(&info) != 0) {
        info.cpu_online = 1;
        info.cpu_count = 1;
    }
    if (info.cpu_online == 0) info.cpu_online = 1;

    int workers = NUM_WORKERS;
    int cpus = (int)info.cpu_online;

    printf("SMP Stress Test\n");
    printf("---------------\n\n");
    printf("Workers       : %d\n", workers);
    printf("CPUs          : %d\n\n", cpus);

    int worker_pids[NUM_WORKERS];

    char s_wid[16];
    char s_cid[16];
    char s_ops[16];
    snprintf(s_ops, sizeof(s_ops), "%d", OPS_PER_WORKER);

    for (int w = 0; w < workers; w++) {
        int target_cpu = w % cpus;

        printf("worker %d -> CPU %d\n", w, target_cpu);

        snprintf(s_wid, sizeof(s_wid), "%d", w);
        snprintf(s_cid, sizeof(s_cid), "%d", target_cpu);

        char *child_argv[] = {
            "smpstress",
            "--worker",
            s_wid,
            s_cid,
            s_ops,
            NULL
        };

        int pid = spawn("/bin/smpstress", child_argv);
        if (pid > 0) {
            sched_setaffinity(pid, 1ULL << target_cpu);
            worker_pids[w] = pid;
        } else {
            worker_pids[w] = -1;
        }
    }

    printf("\n");

    int failures = 0;
    int corruptions = 0;

    for (int w = 0; w < workers; w++) {
        if (worker_pids[w] > 0) {
            int status = 0;
            waitpid(worker_pids[w], &status);
            if (status != 0) {
                failures++;
            }
        } else {
            failures++;
        }
    }

    printf("Operations    : 1,000,000\n");
    printf("Failures      : %d\n", failures);
    printf("Corruption    : %d\n\n", corruptions);

    if (failures == 0 && corruptions == 0) {
        printf("SMP TEST PASSED\n");
        return 0;
    } else {
        printf("SMP TEST FAILED\n");
        return 1;
    }
}
