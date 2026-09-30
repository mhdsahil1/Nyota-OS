/* =============================================================================
 * Nyota OS — System Reboot Utility (/bin/reboot)
 * Initiates controlled system shutdown and reboot through PID 1 supervisor.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (getuid() != 0) {
        printf("reboot: must be root\n");
        return 1;
    }

    printf("Requesting system reboot via PID 1...\n");

    /* Request PID 1 controlled reboot via /run/init.sock */
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd >= 0) {
        struct sockaddr_un sun;
        sun.sun_family = AF_UNIX;
        strcpy(sun.sun_path, "/run/init.sock");
        if (connect(fd, (struct sockaddr *)&sun, sizeof(sun)) == 0) {
            send(fd, "reboot\n", 7, 0);
            char resp[16];
            recv(fd, resp, sizeof(resp) - 1, 0);
            close(fd);
            /* Sleep while PID 1 performs graceful shutdown */
            sleep(2000);
        } else {
            close(fd);
        }
    }

    /* Fallback if PID 1 is unresponsive */
    printf("[reboot] Direct fallback rebooting...\n");
    sync();
    reboot(REBOOT_CMD_REBOOT);

    return 0;
}
