/* =============================================================================
 * Nyota OS — Hostname Utility (/bin/hostname)
 * Reads or updates system hostname stored in /etc/hostname.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    if (argc >= 2) {
        if (getuid() != 0) {
            printf("hostname: permission denied\n");
            return 1;
        }
        int fd = open("/etc/hostname", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            printf("hostname: cannot open /etc/hostname\n");
            return 1;
        }
        write(fd, argv[1], strlen(argv[1]));
        write(fd, "\n", 1);
        close(fd);
        return 0;
    }

    int fd = open("/etc/hostname", O_RDONLY);
    if (fd < 0) {
        printf("nyota\n");
        return 0;
    }

    char buf[64];
    int64_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (n > 0) {
        buf[n] = '\0';
        /* strip trailing newline */
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
            buf[--n] = '\0';
        }
        printf("%s\n", buf);
    } else {
        printf("nyota\n");
    }

    return 0;
}
