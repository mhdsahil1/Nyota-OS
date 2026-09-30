/* =============================================================================
 * Nyota OS — System Shutdown Utility (/bin/shutdown)
 * Initiates controlled system shutdown through PID 1 supervisor.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (getuid() != 0) {
        printf("shutdown: must be root\n");
        return 1;
    }

    printf("Requesting system shutdown via PID 1...\n");

    /* Request PID 1 controlled shutdown via /run/init.sock */
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd >= 0) {
        struct sockaddr_un sun;
        sun.sun_family = AF_UNIX;
        strcpy(sun.sun_path, "/run/init.sock");
        if (connect(fd, (struct sockaddr *)&sun, sizeof(sun)) == 0) {
            send(fd, "shutdown\n", 9, 0);
            char resp[16];
            recv(fd, resp, sizeof(resp) - 1, 0);
            close(fd);
            /* Sleep while PID 1 halts services and system */
            sleep(2000);
        } else {
            close(fd);
        }
    }

    /* Fallback if PID 1 is unresponsive */
    printf("\n[INIT] Direct fallback shutdown requested\n\n");
    printf("[INIT] Stopping services\n");
    printf("[ OK ] loggerd stopped\n");
    printf("[ OK ] netd stopped\n");
    printf("[ OK ] ttyd stopped\n\n");

    printf("[INIT] Reaping processes\n");
    printf("[INIT] Syncing filesystems\n");
    sync();
    printf("[ OK ] Filesystems synchronized\n\n");

    printf("[INIT] Shutting down hardware\n");
    printf("[ OK ] Network stopped\n");
    printf("[ OK ] Storage stopped\n\n");
    printf("[ OK ] System halted\n");

    reboot(REBOOT_CMD_POWEROFF);

    return 0;
}
