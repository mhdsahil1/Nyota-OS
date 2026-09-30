/* =============================================================================
 * Nyota OS — Network Configuration Daemon (/sbin/netd)
 * Parses /etc/network.conf, configures network stack, manages runtime status.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("[netd] Starting network daemon (PID %d)\n", getpid());

    /* Create PID file in /run */
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

    char if_name[16] = "eth0";
    char ip_addr[32] = "10.0.2.15";
    char netmask[32] = "255.255.255.0";
    char gateway[32] = "10.0.2.2";
    char dns[32] = "10.0.2.3";

    /* Read /etc/network.conf if present */
    int fd = open("/etc/network.conf", O_RDONLY);
    if (fd >= 0) {
        char buf[512];
        int64_t bytes = read(fd, buf, sizeof(buf) - 1);
        if (bytes > 0) {
            buf[bytes] = '\0';
            printf("[netd] Loaded configuration from /etc/network.conf\n");
            /* Simple key=value extraction */
            char *p = buf;
            while (*p) {
                while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
                if (!*p) break;
                char *line = p;
                while (*p && *p != '\r' && *p != '\n') p++;
                if (*p) { *p = '\0'; p++; }

                if (strncmp(line, "iface=", 6) == 0) strncpy(if_name, line + 6, sizeof(if_name) - 1);
                else if (strncmp(line, "ip=", 3) == 0) strncpy(ip_addr, line + 3, sizeof(ip_addr) - 1);
                else if (strncmp(line, "netmask=", 8) == 0) strncpy(netmask, line + 8, sizeof(netmask) - 1);
                else if (strncmp(line, "gateway=", 8) == 0) strncpy(gateway, line + 8, sizeof(gateway) - 1);
                else if (strncmp(line, "dns=", 4) == 0) strncpy(dns, line + 4, sizeof(dns) - 1);
            }
        }
        close(fd);
    } else {
        printf("[netd] Using default static interface 10.0.2.15\n");
    }

    /* Configure network interface using ifconfig helper */
    char *if_argv[] = {"/bin/ifconfig", if_name, ip_addr, netmask, NULL};
    int pid = spawn("/bin/ifconfig", if_argv);
    if (pid > 0) {
        int status = 0;
        waitpid(pid, &status);
    }

    /* Create /run/netd.sock for IPC network status queries */
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd >= 0) {
        struct sockaddr_un sun;
        sun.sun_family = AF_UNIX;
        strcpy(sun.sun_path, "/run/netd.sock");
        bind(sock_fd, (struct sockaddr *)&sun, sizeof(sun));
        listen(sock_fd, 5);
    }

    /* netd supervision & IPC query loop */
    while (1) {
        if (sock_fd >= 0) {
            struct sockaddr client_addr;
            size_t addrlen = sizeof(client_addr);
            int client = accept(sock_fd, &client_addr, &addrlen);
            if (client >= 0) {
                char req[64];
                int64_t n = recv(client, req, sizeof(req) - 1, 0);
                if (n > 0) {
                    req[n] = '\0';
                    char resp[256];
                    resp[0] = '\0';
                    strcat(resp, "INTERFACE: "); strcat(resp, if_name); strcat(resp, "\n");
                    strcat(resp, "IP:        "); strcat(resp, ip_addr); strcat(resp, "\n");
                    strcat(resp, "NETMASK:   "); strcat(resp, netmask); strcat(resp, "\n");
                    strcat(resp, "GATEWAY:   "); strcat(resp, gateway); strcat(resp, "\n");
                    strcat(resp, "DNS:       "); strcat(resp, dns); strcat(resp, "\n");
                    strcat(resp, "STATUS:    UP\n");
                    send(client, resp, strlen(resp), 0);
                }
                close(client);
            }
        }
        sleep(200);
    }

    return 0;
}
