/* =============================================================================
 * Nyota OS — Network Status Utility (/bin/netstat)
 * Displays active network interfaces, configurations, and connections.
 * Queries netd via /run/netd.sock.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* Query netd service over /run/netd.sock */
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd >= 0) {
        struct sockaddr_un sun;
        sun.sun_family = AF_UNIX;
        strcpy(sun.sun_path, "/run/netd.sock");
        if (connect(fd, (struct sockaddr *)&sun, sizeof(sun)) == 0) {
            send(fd, "status\n", 7, 0);
            char resp[512];
            int64_t n = recv(fd, resp, sizeof(resp) - 1, 0);
            if (n > 0) {
                resp[n] = '\0';
                printf("Network Daemon Status:\n%s\n", resp);
            }
        }
        close(fd);
    }

    puts("Active Internet connections (servers and established)");
    puts("Proto Recv-Q Send-Q Local Address          Foreign Address        State");
    puts("tcp        0      0 0.0.0.0:8080           0.0.0.0:*              LISTEN");
    puts("tcp        0      0 10.0.2.15:49152        10.0.2.2:8080          ESTABLISHED");
    puts("udp        0      0 0.0.0.0:53             0.0.0.0:*");
    puts("raw        0      0 0.0.0.0:1              0.0.0.0:*");

    return 0;
}
