/* =============================================================================
 * Nyota OS — User Utility: kill (/bin/kill)
 * Sends POSIX signals to processes.
 * =========================================================================== */

#include "libnyota.h"

static int parse_int(const char *s) {
    if (!s) return 0;
    int res = 0;
    int sign = 1;
    if (*s == '-') {
        sign = -1;
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        res = res * 10 + (*s - '0');
        s++;
    }
    return res * sign;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        puts("Usage: kill [-<sig>] <pid>");
        puts("Signals: -TERM (15), -KILL (9), -STOP (19), -CONT (18), -INT (2)");
        return 1;
    }

    int sig = SIGTERM;
    int pid = 0;

    if (argc == 2) {
        pid = parse_int(argv[1]);
    } else {
        const char *sigstr = argv[1];
        if (sigstr[0] == '-') {
            sigstr++;
            if (strcmp(sigstr, "TERM") == 0 || strcmp(sigstr, "15") == 0) {
                sig = SIGTERM;
            } else if (strcmp(sigstr, "KILL") == 0 || strcmp(sigstr, "9") == 0) {
                sig = SIGKILL;
            } else if (strcmp(sigstr, "STOP") == 0 || strcmp(sigstr, "19") == 0) {
                sig = SIGSTOP;
            } else if (strcmp(sigstr, "CONT") == 0 || strcmp(sigstr, "18") == 0) {
                sig = SIGCONT;
            } else if (strcmp(sigstr, "INT") == 0 || strcmp(sigstr, "2") == 0) {
                sig = SIGINT;
            } else if (strcmp(sigstr, "HUP") == 0 || strcmp(sigstr, "1") == 0) {
                sig = SIGHUP;
            } else {
                sig = parse_int(sigstr);
                if (sig <= 0 || sig >= 32) {
                    printf("kill: unknown signal %s\n", argv[1]);
                    return 1;
                }
            }
        }
        pid = parse_int(argv[2]);
    }

    if (pid <= 0) {
        printf("kill: invalid pid '%d'\n", pid);
        return 1;
    }

    int ret = kill(pid, sig);
    if (ret < 0) {
        if (ret == -1) {
            printf("kill: (%d) - Operation not permitted\n", pid);
        } else if (ret == -3) {
            printf("kill: (%d) - No such process\n", pid);
        } else {
            printf("kill: failed to signal pid %d (error %d)\n", pid, ret);
        }
        return 1;
    }

    return 0;
}
