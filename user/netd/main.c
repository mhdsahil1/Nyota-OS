/* =============================================================================
 * Nyota OS — Network Configuration Daemon (/sbin/netd)
 * Parses /etc/network.conf, configures network stack, manages runtime status.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("[netd] Starting network daemon (PID %d)\n", getpid());

    /* Create PID file */
    int pfd = open("/run/netd.pid", O_WRONLY | O_CREAT | O_TRUNC, 0644);
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

    /* Read /etc/network.conf */
    int fd = open("/etc/network.conf", O_RDONLY);
    if (fd >= 0) {
        char buf[512];
        int64_t bytes = read(fd, buf, sizeof(buf) - 1);
        if (bytes > 0) {
            buf[bytes] = '\0';
            printf("[netd] Loaded configuration from /etc/network.conf\n");
        }
        close(fd);
    } else {
        printf("[netd] Using default DHCP / static interface 10.0.2.15\n");
    }

    /* Configure network interface using ifconfig helper */
    char *if_argv[] = {"/bin/ifconfig", "eth0", "10.0.2.15", "255.255.255.0", NULL};
    int pid = spawn("/bin/ifconfig", if_argv);
    if (pid > 0) {
        int status = 0;
        waitpid(pid, &status);
    }

    /* Create /run/netd.sock */
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd >= 0) {
        struct sockaddr_un sun;
        sun.sun_family = AF_UNIX;
        strcpy(sun.sun_path, "/run/netd.sock");
        bind(sock_fd, (struct sockaddr *)&sun, sizeof(sun));
        listen(sock_fd, 5);
    }

    /* Keep netd supervising in background */
    while (1) {
        sleep(5000);
    }

    return 0;
}
