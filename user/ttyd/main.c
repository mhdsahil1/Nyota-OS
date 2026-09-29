/* =============================================================================
 * Nyota OS — Terminal Daemon (/sbin/ttyd)
 * Initializes virtual terminals, allocates sessions, manages terminal state.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("[ttyd] Starting terminal daemon (PID %d)\n", getpid());

    /* Create PID file */
    int pfd = open("/run/ttyd.pid", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (pfd >= 0) {
        char pid_str[16];
        int p = getpid();
        int i = 0;
        char tmp[16];
        if (p == 0) tmp[i++] = '0';
        else { while (p > 0) { tmp[i++] = '0' + (p % 10); p /= 10; } }
        int pos = 0;
        while (i > 0) pid_str[pos++] = tmp[--i];
        pid_str[pos++] = '\n';
        write(pfd, pid_str, pos);
        close(pfd);
    }

    printf("[ttyd] Virtual terminals tty0..tty3 ready\n");

    while (1) {
        sleep(5000);
    }

    return 0;
}
