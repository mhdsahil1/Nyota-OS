/* =============================================================================
 * Nyota OS — System Service Manager Daemon (/sbin/nyotad)
 * Service management interface and companion daemon for PID 1 supervision.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("nyotad: Nyota Service Manager active (PID 1 integration)\n");
        printf("Usage: nyotad <list|start|stop|restart|status> [service]\n");
        return 0;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        printf("nyotad: failed to create socket\n");
        return 1;
    }

    struct sockaddr_un sun;
    sun.sun_family = AF_UNIX;
    strcpy(sun.sun_path, "/run/init.sock");
    if (connect(fd, (struct sockaddr *)&sun, sizeof(sun)) < 0) {
        printf("nyotad: could not connect to initd daemon at /run/init.sock\n");
        close(fd);
        return 1;
    }

    char req[128];
    req[0] = '\0';
    strcat(req, argv[1]);
    if (argc >= 3) {
        strcat(req, " ");
        strcat(req, argv[2]);
    }

    send(fd, req, strlen(req), 0);

    char resp[1024];
    int64_t n = recv(fd, resp, sizeof(resp) - 1, 0);
    if (n > 0) {
        resp[n] = '\0';
        printf("%s", resp);
    }

    close(fd);
    return 0;
}
