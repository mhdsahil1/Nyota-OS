/* =============================================================================
 * Nyota OS — Logger Utility (/bin/logger)
 * Submits log messages to loggerd via /run/logger.sock and /var/log/system.log
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: logger <message>\n");
        return 1;
    }

    /* Build log message string */
    char msg[256];
    msg[0] = '\0';
    for (int i = 1; i < argc; i++) {
        strcat(msg, argv[i]);
        if (i < argc - 1) strcat(msg, " ");
    }
    strcat(msg, "\n");

    /* Send via /run/logger.sock */
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd >= 0) {
        struct sockaddr_un sun;
        sun.sun_family = AF_UNIX;
        strcpy(sun.sun_path, "/run/logger.sock");
        if (connect(fd, (struct sockaddr *)&sun, sizeof(sun)) == 0) {
            send(fd, msg, strlen(msg), 0);
        }
        close(fd);
    }

    /* Ensure log reaches /var/log/system.log */
    int lfd = open("/var/log/system.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (lfd >= 0) {
        write(lfd, msg, strlen(msg));
        close(lfd);
    }

    /* Submit to kernel log */
    klog(1, msg, strlen(msg));
    return 0;
}
