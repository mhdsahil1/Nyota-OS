/* =============================================================================
 * Nyota OS — HTTP/1.1 Client Utility (/bin/httpget)
 * Performs HTTP GET requests over TCP with status and header parsing.
 * =========================================================================== */

#include "libnyota.h"

static uint32_t resolve_host(const char *host) {
    /* Check if already an IP address */
    uint32_t ip = inet_addr(host);
    if (ip != 0 && ip != 0xFFFFFFFF) return ip;

    if (strcmp(host, "localhost") == 0) return inet_addr("127.0.0.1");

    /* Check /etc/hosts */
    int fd = open("/etc/hosts", O_RDONLY);
    if (fd >= 0) {
        char buf[1024];
        int64_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n > 0) {
            buf[n] = '\0';
            char *line = buf;
            while (*line) {
                while (*line == ' ' || *line == '\t' || *line == '\n' || *line == '\r') line++;
                if (!*line) break;
                char *next = line;
                while (*next && *next != '\n') next++;
                if (*next) { *next = '\0'; next++; }

                /* Parse ip and names */
                char ip_part[32] = "";
                char name_part[64] = "";
                char *p = line;
                int i = 0;
                while (*p && *p != ' ' && *p != '\t' && i < 31) ip_part[i++] = *p++;
                ip_part[i] = '\0';
                while (*p == ' ' || *p == '\t') p++;
                while (*p) {
                    i = 0;
                    while (*p && *p != ' ' && *p != '\t' && i < 63) name_part[i++] = *p++;
                    name_part[i] = '\0';
                    if (strcmp(name_part, host) == 0) {
                        uint32_t res = inet_addr(ip_part);
                        if (res != 0) return res;
                    }
                    while (*p == ' ' || *p == '\t') p++;
                }
                line = next;
            }
        }
    }

    /* Fallback default emulator host for example.com */
    if (strcmp(host, "example.com") == 0) return inet_addr("93.184.215.14");

    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        puts("Usage: httpget [-o output_file] <http://host[:port]/path>");
        return 1;
    }

    const char *out_file = NULL;
    const char *url = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_file = argv[++i];
        } else {
            url = argv[i];
        }
    }

    if (!url) {
        puts("httpget: missing URL");
        return 1;
    }

    /* Validate HTTP scheme */
    if (strncmp(url, "http://", 7) != 0) {
        puts("httpget: only http:// URLs are supported");
        return 1;
    }

    const char *host_start = url + 7;
    const char *slash = strchr(host_start, '/');
    char host[128];
    char path[256];
    uint16_t port = 80;

    if (slash) {
        size_t hlen = (size_t)(slash - host_start);
        if (hlen >= sizeof(host)) hlen = sizeof(host) - 1;
        strncpy(host, host_start, hlen);
        host[hlen] = '\0';
        strncpy(path, slash, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    } else {
        strncpy(host, host_start, sizeof(host) - 1);
        host[sizeof(host) - 1] = '\0';
        strcpy(path, "/");
    }

    /* Check for explicit port */
    char *colon = strchr(host, ':');
    if (colon) {
        *colon = '\0';
        port = (uint16_t)atoi(colon + 1);
    }

    printf("Connecting to %s:%u...\n", host, port);

    uint32_t ip = resolve_host(host);
    if (ip == 0) {
        printf("httpget: cannot resolve host '%s'\n", host);
        return 1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        puts("httpget: socket creation failed");
        return 1;
    }

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port);
    saddr.sin_addr.s_addr = ip;

    if (connect(sock, (const struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        puts("httpget: connection failed");
        close(sock);
        return 1;
    }

    /* Construct HTTP/1.1 GET Request */
    char req[1024];
    int req_len = snprintf(req, sizeof(req),
                           "GET %s HTTP/1.1\r\n"
                           "Host: %s\r\n"
                           "User-Agent: NyotaOS-httpget/1.3.0\r\n"
                           "Connection: close\r\n"
                           "\r\n",
                           path, host);

    send(sock, req, req_len, 0);

    /* Read HTTP Response */
    char resp_buf[4096];
    int64_t total_read = 0;
    int out_fd = -1;
    if (out_file) {
        out_fd = open(out_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    }

    bool header_parsed = false;
    int status_code = 0;
    int body_offset = 0;

    while (1) {
        char chunk[1024];
        int64_t n = recv(sock, chunk, sizeof(chunk) - 1, 0);
        if (n <= 0) break;
        chunk[n] = '\0';

        if (!header_parsed) {
            /* Copy into response buffer to parse headers */
            int to_copy = (int)n;
            if (total_read + to_copy > (int)sizeof(resp_buf) - 1) {
                to_copy = sizeof(resp_buf) - 1 - total_read;
            }
            memcpy(resp_buf + total_read, chunk, to_copy);
            total_read += to_copy;
            resp_buf[total_read] = '\0';

            char *hdr_end = strstr(resp_buf, "\r\n\r\n");
            if (hdr_end) {
                header_parsed = true;
                body_offset = (int)(hdr_end - resp_buf) + 4;

                /* Parse status code */
                if (strncmp(resp_buf, "HTTP/", 5) == 0) {
                    char *sp = strchr(resp_buf, ' ');
                    if (sp) status_code = atoi(sp + 1);
                }

                printf("HTTP Response: %d\n", status_code);

                /* Write/print body part already in buffer */
                int initial_body_len = total_read - body_offset;
                if (initial_body_len > 0) {
                    if (out_fd >= 0) write(out_fd, resp_buf + body_offset, initial_body_len);
                    else printf("%s", resp_buf + body_offset);
                }
            }
        } else {
            /* Subsequent chunks are pure body */
            if (out_fd >= 0) write(out_fd, chunk, n);
            else printf("%s", chunk);
        }
    }

    close(sock);
    if (out_fd >= 0) close(out_fd);

    if (!header_parsed && total_read > 0) {
        printf("%s\n", resp_buf);
    }

    return (status_code >= 200 && status_code < 400) ? 0 : 1;
}
