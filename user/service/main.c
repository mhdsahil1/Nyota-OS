/* =============================================================================
 * Nyota OS — Service Management Utility (/bin/service)
 * Connects to /run/init.sock to query, start, stop, and restart services.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: service <list|start|stop|restart|status> [name]\n");
        return 1;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        printf("service: failed to create socket\n");
        return 1;
    }

    struct sockaddr_un sun;
    sun.sun_family = AF_UNIX;
    strcpy(sun.sun_path, "/run/init.sock");
    if (connect(fd, (struct sockaddr *)&sun, sizeof(sun)) < 0) {
        printf("service: could not connect to initd daemon at /run/init.sock\n");
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
