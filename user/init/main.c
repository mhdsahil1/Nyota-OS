/* =============================================================================
 * Nyota OS — Init Process & Service Manager (/init)
 * PID 1 initialization process, service supervisor, orphan reaper, IPC server.
 * =========================================================================== */

#include "libnyota.h"

#define MAX_SERVICES 16

typedef enum {
    SERVICE_STOPPED = 0,
    SERVICE_STARTING,
    SERVICE_RUNNING,
    SERVICE_STOPPING,
    SERVICE_FAILED
} service_state_t;

typedef enum {
    RESTART_NEVER = 0,
    RESTART_ALWAYS,
    RESTART_ON_FAILURE
} restart_policy_t;

typedef struct {
    char name[32];
    char command[64];
    restart_policy_t restart_policy;
    service_state_t state;
    int pid;
    int restart_count;
    uint64_t start_time;
    int exit_status;
} service_t;

static service_t services[MAX_SERVICES];
static size_t num_services = 0;
static int init_sock_fd = -1;

static const char *state_to_str(service_state_t state) {
    switch (state) {
        case SERVICE_STOPPED:  return "STOPPED";
        case SERVICE_STARTING: return "STARTING";
        case SERVICE_RUNNING:  return "RUNNING";
        case SERVICE_STOPPING: return "STOPPING";
        case SERVICE_FAILED:   return "FAILED";
        default:               return "UNKNOWN";
    }
}

static service_t *find_service(const char *name) {
    for (size_t i = 0; i < num_services; i++) {
        if (strcmp(services[i].name, name) == 0) {
            return &services[i];
        }
    }
    return NULL;
}

static service_t *find_service_by_pid(int pid) {
    if (pid <= 0) return NULL;
    for (size_t i = 0; i < num_services; i++) {
        if (services[i].pid == pid) {
            return &services[i];
        }
    }
    return NULL;
}

static int start_service(service_t *s) {
    if (!s) return -1;
    s->state = SERVICE_STARTING;

    char *argv[] = {s->command, NULL};
    int pid = spawn(s->command, argv);
    if (pid > 0) {
        s->pid = pid;
        s->state = SERVICE_RUNNING;
        s->start_time = (uint64_t)time(NULL);
        printf("[INIT] Started service %s (PID %d)\n", s->name, pid);
        return 0;
    } else {
        s->state = SERVICE_FAILED;
        printf("[INIT] Failed to start service %s\n", s->name);
        return -1;
    }
}

static void stop_service(service_t *s) {
    if (!s || s->pid <= 0) return;
    s->state = SERVICE_STOPPING;
    kill(s->pid, SIGTERM);
    s->state = SERVICE_STOPPED;
    s->pid = 0;
    printf("[INIT] Stopped service %s\n", s->name);
}

static void restart_service(service_t *s) {
    if (!s) return;
    if (s->pid > 0) {
        stop_service(s);
        sleep(200);
    }
    s->restart_count++;
    start_service(s);
}

static void register_service(const char *name, const char *cmd, restart_policy_t policy) {
    if (num_services >= MAX_SERVICES) return;
    service_t *s = &services[num_services++];
    memset(s, 0, sizeof(service_t));
    strncpy(s->name, name, sizeof(s->name) - 1);
    strncpy(s->command, cmd, sizeof(s->command) - 1);
    s->restart_policy = policy;
    s->state = SERVICE_STOPPED;
}

static void load_init_config(void) {
    /* Register default core services */
    register_service("logger", "/sbin/loggerd", RESTART_ALWAYS);
    register_service("ttyd", "/sbin/ttyd", RESTART_ALWAYS);
    register_service("netd", "/sbin/netd", RESTART_ON_FAILURE);
    register_service("logind", "/sbin/logind", RESTART_ALWAYS);
}

static void handle_ipc_command(int client_fd) {
    char req[128];
    int64_t n = recv(client_fd, req, sizeof(req) - 1, 0);
    if (n <= 0) {
        close(client_fd);
        return;
    }
    req[n] = '\0';

    /* Parse commands: list, start <name>, stop <name>, restart <name>, status <name> */
    if (strncmp(req, "list", 4) == 0) {
        char resp[1024];
        resp[0] = '\0';
        strcat(resp, "NAME        PID    STATE     RESTARTS\n");
        strcat(resp, "-------------------------------------\n");
        for (size_t i = 0; i < num_services; i++) {
            char line[128];
            char pid_buf[16];
            int p = services[i].pid;
            int pi = 0;
            char tmp[16];
            if (p == 0) tmp[pi++] = '-';
            else { while (p > 0) { tmp[pi++] = '0' + (p % 10); p /= 10; } }
            int pos = 0;
            while (pi > 0) pid_buf[pos++] = tmp[--pi];
            pid_buf[pos] = '\0';

            char r_buf[16];
            int r = services[i].restart_count;
            int ri = 0;
            if (r == 0) tmp[ri++] = '0';
            else { while (r > 0) { tmp[ri++] = '0' + (r % 10); r /= 10; } }
            pos = 0;
            while (ri > 0) r_buf[pos++] = tmp[--ri];
            r_buf[pos] = '\0';

            strcpy(line, services[i].name);
            while (strlen(line) < 12) strcat(line, " ");
            strcat(line, pid_buf);
            while (strlen(line) < 19) strcat(line, " ");
            strcat(line, state_to_str(services[i].state));
            while (strlen(line) < 29) strcat(line, " ");
            strcat(line, r_buf);
            strcat(line, "\n");
            strcat(resp, line);
        }
        send(client_fd, resp, strlen(resp), 0);
    } else if (strncmp(req, "start ", 6) == 0) {
        char *target = req + 6;
        while (*target == ' ') target++;
        service_t *s = find_service(target);
        if (s) {
            start_service(s);
            send(client_fd, "OK\n", 3, 0);
        } else {
            send(client_fd, "ERR_NOT_FOUND\n", 14, 0);
        }
    } else if (strncmp(req, "stop ", 5) == 0) {
        char *target = req + 5;
        while (*target == ' ') target++;
        service_t *s = find_service(target);
        if (s) {
            stop_service(s);
            send(client_fd, "OK\n", 3, 0);
        } else {
            send(client_fd, "ERR_NOT_FOUND\n", 14, 0);
        }
    } else if (strncmp(req, "restart ", 8) == 0) {
        char *target = req + 8;
        while (*target == ' ') target++;
        service_t *s = find_service(target);
        if (s) {
            restart_service(s);
            send(client_fd, "OK\n", 3, 0);
        } else {
            send(client_fd, "ERR_NOT_FOUND\n", 14, 0);
        }
    } else if (strncmp(req, "status ", 7) == 0) {
        char *target = req + 7;
        while (*target == ' ') target++;
        service_t *s = find_service(target);
        if (s) {
            char resp[256];
            resp[0] = '\0';
            strcat(resp, "Service: "); strcat(resp, s->name); strcat(resp, "\n");
            strcat(resp, "Command: "); strcat(resp, s->command); strcat(resp, "\n");
            strcat(resp, "State:   "); strcat(resp, state_to_str(s->state)); strcat(resp, "\n");
            send(client_fd, resp, strlen(resp), 0);
        } else {
            send(client_fd, "ERR_NOT_FOUND\n", 14, 0);
        }
    } else {
        send(client_fd, "UNKNOWN_COMMAND\n", 16, 0);
    }

    close(client_fd);
}

static void sigchld_handler(int sig) {
    (void)sig;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    signal(SIGCHLD, sigchld_handler);

    printf("\n");
    printf("========================================\n");
    printf("              NYOTA OS                  \n");
    printf("========================================\n");
    printf("Kernel       : v0.9.0\n");
    printf("Architecture : x86_64\n\n");
    printf("[INIT] Starting userspace supervisor\n\n");

    load_init_config();

    /* Start services sequentially in dependency order */
    for (size_t i = 0; i < num_services; i++) {
        start_service(&services[i]);
        sleep(100);
    }

    printf("\n[INIT] All configured services initialized.\n");

    /* Create /run/init.sock for service control */
    init_sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (init_sock_fd >= 0) {
        struct sockaddr_un sun;
        sun.sun_family = AF_UNIX;
        strcpy(sun.sun_path, "/run/init.sock");
        bind(init_sock_fd, (struct sockaddr *)&sun, sizeof(sun));
        listen(init_sock_fd, 5);
    }

    /* Main init supervision loop */
    while (1) {
        /* Check IPC commands */
        if (init_sock_fd >= 0) {
            struct sockaddr client_addr;
            size_t addrlen = sizeof(client_addr);
            int client = accept(init_sock_fd, &client_addr, &addrlen);
            if (client >= 0) {
                handle_ipc_command(client);
            }
        }

        /* Reap all terminated children (services or orphaned processes) */
        int status = 0;
        int pid;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            service_t *s = find_service_by_pid(pid);
            if (s) {
                s->exit_status = status;
                printf("[INIT] Service %s (PID %d) exited with status %d\n", s->name, pid, status);

                bool should_restart = false;
                if (s->restart_policy == RESTART_ALWAYS) {
                    should_restart = true;
                } else if (s->restart_policy == RESTART_ON_FAILURE && status != 0) {
                    should_restart = true;
                }

                if (should_restart) {
                    if (s->restart_count < 5) {
                        s->restart_count++;
                        printf("[INIT] Restarting %s (attempt %d/5)...\n", s->name, s->restart_count);
                        sleep(300);
                        start_service(s);
                    } else {
                        printf("[INIT] Service %s reached restart limit. Marked FAILED.\n", s->name);
                        s->state = SERVICE_FAILED;
                        s->pid = 0;
                    }
                } else {
                    s->state = SERVICE_STOPPED;
                    s->pid = 0;
                }
            } else {
                printf("[INIT] Reaped child PID %d (status %d)\n", pid, status);
            }
        }

        sleep(100);
    }

    return 0;
}
