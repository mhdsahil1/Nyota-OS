/* =============================================================================
 * Nyota OS — System Logging Daemon (/sbin/loggerd)
 * Receives logs via /run/logger.sock and drains kernel ring buffer to /var/log/
 * Implements bounded log rotation (.old).
 * =========================================================================== */

#include "libnyota.h"

#define MAX_LOG_SIZE 16384

static void rotate_file(const char *path) {
    char old_path[64];
    int len = 0;
    while (path[len] && len < 50) {
        old_path[len] = path[len];
        len++;
    }
    old_path[len++] = '.';
    old_path[len++] = 'o';
    old_path[len++] = 'l';
    old_path[len++] = 'd';
    old_path[len] = '\0';

    /* Copy path content to old_path */
    int rfd = open(path, O_RDONLY, 0);
    if (rfd >= 0) {
        int wfd = open(old_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (wfd >= 0) {
            char buf[512];
            int n;
            while ((n = read(rfd, buf, sizeof(buf))) > 0) {
                write(wfd, buf, n);
            }
            close(wfd);
        }
        close(rfd);
    }

    /* Truncate path */
    int tfd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (tfd >= 0) {
        write(tfd, "[LOG ROTATED]\n", 14);
        close(tfd);
    }
}

static void append_to_file(const char *path, const char *msg) {
    stat_t st;
    if (stat(path, &st) == 0 && st.size > MAX_LOG_SIZE) {
        rotate_file(path);
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        write(fd, msg, strlen(msg));
        close(fd);
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("[loggerd] Starting system logger daemon (PID %d)\n", getpid());

    /* Ensure log directory files exist */
    append_to_file("/var/log/kernel.log", "");
    append_to_file("/var/log/system.log", "[INFO] loggerd started successfully\n");
    append_to_file("/var/log/services.log", "");

    /* Create PID file in /run */
    int pfd = open("/run/loggerd.pid", O_WRONLY | O_CREAT | O_TRUNC, 0644);
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

    /* Initialize Unix domain socket /run/logger.sock */
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd >= 0) {
        struct sockaddr_un sun;
        sun.sun_family = AF_UNIX;
        strcpy(sun.sun_path, "/run/logger.sock");
        bind(sock_fd, (struct sockaddr *)&sun, sizeof(sun));
        listen(sock_fd, 5);
    }

    /* Main logging loop */
    char klog_buf[1024];
    while (1) {
        /* Drain kernel ring buffer to /var/log/kernel.log */
        int bytes = klog(2, klog_buf, sizeof(klog_buf) - 1);
        if (bytes > 0) {
            klog_buf[bytes] = '\0';
            append_to_file("/var/log/kernel.log", klog_buf);
        }

        /* Check for incoming messages on /run/logger.sock */
        if (sock_fd >= 0) {
            struct sockaddr client_addr;
            size_t addrlen = sizeof(client_addr);
            int client_fd = accept(sock_fd, &client_addr, &addrlen);
            if (client_fd >= 0) {
                char msg[256];
                int64_t n = recv(client_fd, msg, sizeof(msg) - 1, 0);
                if (n > 0) {
                    msg[n] = '\0';
                    if (strncmp(msg, "[SERVICE]", 9) == 0) {
                        append_to_file("/var/log/services.log", msg);
                    } else {
                        append_to_file("/var/log/system.log", msg);
                    }
                }
                close(client_fd);
            }
        }

        sleep(100);
    }

    return 0;
}
