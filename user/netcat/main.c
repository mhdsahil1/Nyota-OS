/* =============================================================================
 * Nyota OS — Network Cat Utility (/bin/netcat)
 * TCP client connection, data transmission, and server response reception.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    if (argc < 3) {
        puts("Usage: netcat <ip-address> <port> [message]");
        return 1;
    }

    const char *target_ip_str = argv[1];
    uint32_t target_ip = inet_addr(target_ip_str);
    int port = 0;
    const char *p = argv[2];
    while (*p >= '0' && *p <= '9') {
        port = port * 10 + (*p++ - '0');
    }

    if (port <= 0 || port > 65535) {
        puts("netcat: invalid port number");
        return 1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        puts("netcat: failed to create TCP socket");
        return 1;
    }

    struct sockaddr_in target;
    memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET;
    target.sin_port = htons((uint16_t)port);
    target.sin_addr.s_addr = target_ip;

    printf("Connecting to %s:%d...\n", target_ip_str, port);

    if (connect(sock, (const struct sockaddr *)&target, sizeof(target)) != 0) {
        printf("netcat: connection to %s:%d failed\n", target_ip_str, port);
        close(sock);
        return 1;
    }

    printf("Connected to %s:%d!\n", target_ip_str, port);

    /* Send payload if provided */
    if (argc >= 4) {
        const char *msg = argv[3];
        size_t len = strlen(msg);
        send(sock, msg, len, 0);
        send(sock, "\n", 1, 0);

        char recv_buf[256];
        int64_t n = recv(sock, recv_buf, sizeof(recv_buf) - 1, 0);
        if (n > 0) {
            recv_buf[n] = '\0';
            printf("Received: %s", recv_buf);
        }
    } else {
        const char *default_msg = "Hello from Nyota Netcat!\n";
        send(sock, default_msg, strlen(default_msg), 0);

        char recv_buf[256];
        int64_t n = recv(sock, recv_buf, sizeof(recv_buf) - 1, 0);
        if (n > 0) {
            recv_buf[n] = '\0';
            printf("Received: %s", recv_buf);
        }
    }

    close(sock);
    return 0;
}
