/* =============================================================================
 * Nyota OS — Logger Utility (/bin/logger)
 * Submits log messages to loggerd via /run/logger.sock or directly to klog.
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
    strcat(msg, "[USER] ");
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
            close(fd);
            return 0;
        }
        close(fd);
    }

    /* Fallback directly to kernel log */
    klog(1, msg, strlen(msg));
    return 0;
}
