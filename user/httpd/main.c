/* =============================================================================
 * Nyota OS — Minimal HTTP/1.1 Server Daemon (/bin/httpd)
 * Serves static files from document root with strict path traversal security.
 * =========================================================================== */

#include "libnyota.h"

static const char *get_mime_type(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0) return "text/html";
    if (strcmp(dot, ".css") == 0) return "text/css";
    if (strcmp(dot, ".js") == 0) return "application/javascript";
    if (strcmp(dot, ".txt") == 0) return "text/plain";
    if (strcmp(dot, ".png") == 0) return "image/png";
    return "text/plain";
}

/* Security: validate and canonicalize path within docroot */
static int resolve_safe_path(const char *docroot, const char *url_path, char *out_path, size_t out_max) {
    /* Reject directory traversal attempts */
    if (strstr(url_path, "..")) {
        return -1; /* Forbidden / Traversal */
    }

    /* Handle root or directory */
    const char *sub = url_path;
    while (*sub == '/') sub++;

    if (*sub == '\0') {
        snprintf(out_path, out_max, "%s/index.html", docroot);
    } else {
        snprintf(out_path, out_max, "%s/%s", docroot, sub);
    }

    return 0;
}

static void send_response(int client_fd, int code, const char *status, const char *mime, const void *body, size_t body_len) {
    char header[512];
    int hlen = snprintf(header, sizeof(header),
                        "HTTP/1.1 %d %s\r\n"
                        "Server: NyotaOS-httpd/1.3.0\r\n"
                        "Content-Type: %s\r\n"
                        "Content-Length: %u\r\n"
                        "Connection: close\r\n"
                        "\r\n",
                        code, status, mime, (uint32_t)body_len);
    send(client_fd, header, hlen, 0);
    if (body && body_len > 0) {
        send(client_fd, body, body_len, 0);
    }
}

int main(int argc, char **argv) {
    uint16_t port = 8080;
    const char *docroot = "/var/www";
    bool single_request = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = (uint16_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            docroot = argv[++i];
        } else if (strcmp(argv[i], "-1") == 0) {
            single_request = true;
        }
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd < 0) {
        puts("httpd: failed to create socket");
        return 1;
    }

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port);
    saddr.sin_addr.s_addr = 0; /* INADDR_ANY */

    if (bind(server_fd, (const struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        printf("httpd: failed to bind port %u\n", port);
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 5) < 0) {
        puts("httpd: failed to listen");
        close(server_fd);
        return 1;
    }

    printf("Nyota HTTP Server listening on port %u (docroot: %s)\n", port, docroot);

    while (1) {
        struct sockaddr client_addr;
        size_t addrlen = sizeof(client_addr);
        int client = accept(server_fd, &client_addr, &addrlen);
        if (client < 0) {
            sleep(50);
            continue;
        }

        char req_buf[2048];
        int64_t n = recv(client, req_buf, sizeof(req_buf) - 1, 0);
        if (n <= 0) {
            close(client);
            if (single_request) break;
            continue;
        }
        req_buf[n] = '\0';

        /* Parse request line */
        char method[16] = "";
        char url_path[256] = "";
        char proto[16] = "";

        char *line_end = strstr(req_buf, "\r\n");
        if (!line_end) line_end = strchr(req_buf, '\n');

        if (line_end) {
            *line_end = '\0';
            char *p = req_buf;
            int i = 0;
            while (*p && *p != ' ' && i < (int)sizeof(method) - 1) method[i++] = *p++;
            method[i] = '\0';
            while (*p == ' ') p++;
            i = 0;
            while (*p && *p != ' ' && *p != '\r' && *p != '\n' && i < (int)sizeof(url_path) - 1) url_path[i++] = *p++;
            url_path[i] = '\0';
            while (*p == ' ') p++;
            i = 0;
            while (*p && *p != ' ' && *p != '\r' && *p != '\n' && i < (int)sizeof(proto) - 1) proto[i++] = *p++;
            proto[i] = '\0';
        }

        printf("[httpd] %s %s\n", method, url_path);

        if (strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0) {
            const char *msg = "<html><body><h1>501 Not Implemented</h1></body></html>";
            send_response(client, 501, "Not Implemented", "text/html", msg, strlen(msg));
            close(client);
            if (single_request) break;
            continue;
        }

        char filepath[512];
        if (resolve_safe_path(docroot, url_path, filepath, sizeof(filepath)) < 0) {
            /* Path traversal attempt! */
            const char *msg = "<html><body><h1>403 Forbidden - Path Traversal Detected</h1></body></html>";
            send_response(client, 403, "Forbidden", "text/html", msg, strlen(msg));
            close(client);
            if (single_request) break;
            continue;
        }

        int fd = open(filepath, O_RDONLY);
        if (fd < 0) {
            const char *msg = "<html><body><h1>404 Not Found</h1></body></html>";
            send_response(client, 404, "Not Found", "text/html", msg, strlen(msg));
        } else {
            char file_buf[8192];
            int64_t bytes = read(fd, file_buf, sizeof(file_buf));
            close(fd);
            if (bytes < 0) bytes = 0;

            const char *mime = get_mime_type(filepath);
            send_response(client, 200, "OK", mime, file_buf, (size_t)bytes);
        }

        close(client);
        if (single_request) break;
    }

    close(server_fd);
    return 0;
}
