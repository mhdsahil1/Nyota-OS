/* =============================================================================
 * Nyota OS — TCP Echo Server Utility (/bin/echo-server)
 * Listens on a designated TCP port, accepts incoming connections, and echoes payload.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    int port = 8080;

    if (argc >= 2) {
        port = 0;
        const char *p = argv[1];
        while (*p >= '0' && *p <= '9') {
            port = port * 10 + (*p++ - '0');
        }
        if (port <= 0 || port > 65535) port = 8080;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd < 0) {
        puts("echo-server: failed to create TCP socket");
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t)port);
    server_addr.sin_addr.s_addr = 0; /* INADDR_ANY (0.0.0.0) */

    if (bind(server_fd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        printf("echo-server: failed to bind to port %d\n", port);
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 5) != 0) {
        puts("echo-server: listen failed");
        close(server_fd);
        return 1;
    }

    printf("Listening on 0.0.0.0:%d...\n", port);

    /* Accept incoming connection */
    struct sockaddr_in client_addr;
    size_t addr_len = sizeof(client_addr);
    int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);

    if (client_fd < 0) {
        puts("echo-server: accept failed");
        close(server_fd);
        return 1;
    }

    printf("Client connected from %s:%d\n",
           inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    char buf[512];
    int64_t bytes = 0;

    while ((bytes = recv(client_fd, buf, sizeof(buf), 0)) > 0) {
        send(client_fd, buf, (size_t)bytes, 0);
    }

    puts("Client disconnected.");
    close(client_fd);
    close(server_fd);

    return 0;
}
