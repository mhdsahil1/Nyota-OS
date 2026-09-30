/* =============================================================================
 * Nyota OS — Init Process & Service Manager (/sbin/init)
 * PID 1 initialization process, service supervisor, orphan reaper, IPC server.
 * =========================================================================== */

#include "libnyota.h"

#define MAX_SERVICES 16

typedef enum {
    INIT_RUNNING = 0,
    INIT_SHUTTING_DOWN,
    INIT_HALTED
} init_state_t;

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
    uint64_t last_start_time;
    uint64_t last_exit_time;
    int exit_status;
    char depends[32];
} service_t;

static service_t services[MAX_SERVICES];
static size_t num_services = 0;
static int init_sock_fd = -1;
static init_state_t system_init_state = INIT_RUNNING;

static void int_to_str(int n, char *out) {
    if (n == 0) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    char tmp[16];
    int ti = 0;
    int sign = 0;
    if (n < 0) { sign = 1; n = -n; }
    while (n > 0) {
        tmp[ti++] = '0' + (n % 10);
        n /= 10;
    }
    int pos = 0;
    if (sign) out[pos++] = '-';
    while (ti > 0) out[pos++] = tmp[--ti];
    out[pos] = '\0';
}

static void format_2d(uint32_t val, char *out) {
    out[0] = '0' + ((val / 10) % 10);
    out[1] = '0' + (val % 10);
    out[2] = '\0';
}

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

static void log_service_event(const char *level, const char *service_name, const char *msg) {
    (void)service_name;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t sec = (uint64_t)ts.tv_sec;
    uint32_t hours = (uint32_t)((sec / 3600) % 24);
    uint32_t mins = (uint32_t)((sec % 3600) / 60);
    uint32_t secs = (uint32_t)(sec % 60);

    char line[256];
    char t2[4];
    line[0] = '[';
    line[1] = '\0';
    format_2d(hours, t2); strcat(line, t2);
    strcat(line, ":");
    format_2d(mins, t2); strcat(line, t2);
    strcat(line, ":");
    format_2d(secs, t2); strcat(line, t2);
    strcat(line, "] ");
    strcat(line, level);
    strcat(line, " service: ");
    strcat(line, msg);
    strcat(line, "\n");

    /* Print to console / serial */
    printf("%s", line);

    /* Write to kernel log ring buffer */
    int klog_lvl = 1;
    if (strcmp(level, "DEBUG") == 0) klog_lvl = 0;
    else if (strcmp(level, "WARN") == 0) klog_lvl = 2;
    else if (strcmp(level, "ERROR") == 0) klog_lvl = 3;
    klog(klog_lvl, line, strlen(line));

    /* Append directly to persistent log /var/log/services.log */
    int fd = open("/var/log/services.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        write(fd, line, strlen(line));
        close(fd);
    }
}

static service_t *find_service(const char *name) {
    if (!name || !name[0]) return NULL;

    /* 1. Direct name match */
    for (size_t i = 0; i < num_services; i++) {
        if (strcmp(services[i].name, name) == 0) {
            return &services[i];
        }
    }

    /* 2. Common aliases for seamless compatibility */
    for (size_t i = 0; i < num_services; i++) {
        if (strcmp(name, "logger") == 0 && strcmp(services[i].name, "loggerd") == 0) return &services[i];
        if (strcmp(name, "loggerd") == 0 && strcmp(services[i].name, "logger") == 0) return &services[i];
        if (strcmp(name, "login") == 0 && strcmp(services[i].name, "logind") == 0) return &services[i];
        if (strcmp(name, "logind") == 0 && strcmp(services[i].name, "login") == 0) return &services[i];
        if (strcmp(name, "net") == 0 && strcmp(services[i].name, "netd") == 0) return &services[i];
        if (strcmp(name, "network") == 0 && strcmp(services[i].name, "netd") == 0) return &services[i];
        if (strcmp(name, "netd") == 0 && (strcmp(services[i].name, "net") == 0 || strcmp(services[i].name, "network") == 0)) return &services[i];
        if (strcmp(name, "tty") == 0 && strcmp(services[i].name, "ttyd") == 0) return &services[i];
        if (strcmp(name, "ttyd") == 0 && strcmp(services[i].name, "tty") == 0) return &services[i];
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

static int start_service(service_t *s);

static void stop_service(service_t *s) {
    if (!s || s->pid <= 0) {
        if (s) s->state = SERVICE_STOPPED;
        return;
    }
    s->state = SERVICE_STOPPING;
    char stop_msg[128];
    strcpy(stop_msg, "stopping ");
    strcat(stop_msg, s->name);
    log_service_event("INFO", s->name, stop_msg);

    int target_pid = s->pid;
    kill(target_pid, SIGTERM);

    /* Wait briefly for child process to terminate gracefully */
    int status = 0;
    int reaped = 0;
    for (int i = 0; i < 5; i++) {
        int r = waitpid(target_pid, &status, WNOHANG);
        if (r == target_pid) {
            reaped = 1;
            break;
        }
        sleep(50);
    }
    if (!reaped) {
        kill(target_pid, SIGKILL);
        for (int i = 0; i < 5; i++) {
            int r = waitpid(target_pid, &status, WNOHANG);
            if (r == target_pid) {
                reaped = 1;
                break;
            }
            sleep(50);
        }
    }

    s->state = SERVICE_STOPPED;
    s->pid = 0;

    char pid_path[64];
    strcpy(pid_path, "/run/");
    strcat(pid_path, s->name);
    strcat(pid_path, ".pid");
    unlink(pid_path);

    char stopped_msg[128];
    strcpy(stopped_msg, s->name);
    strcat(stopped_msg, " stopped");
    log_service_event("INFO", s->name, stopped_msg);
}

static int start_service(service_t *s) {
    if (!s) return -1;
    if (system_init_state != INIT_RUNNING) {
        log_service_event("WARN", s->name, "cannot start service during shutdown");
        return -1;
    }
    if (s->state == SERVICE_RUNNING && s->pid > 0) return 0;

    /* Check dependency */
    if (s->depends[0] != '\0') {
        service_t *dep = find_service(s->depends);
        if (dep) {
            if (dep->state != SERVICE_RUNNING || dep->pid <= 0) {
                char dep_msg[128];
                strcpy(dep_msg, "starting dependency ");
                strcat(dep_msg, dep->name);
                strcat(dep_msg, " for ");
                strcat(dep_msg, s->name);
                log_service_event("INFO", s->name, dep_msg);

                if (start_service(dep) != 0 || dep->state != SERVICE_RUNNING) {
                    char err_msg[128];
                    strcpy(err_msg, "dependency ");
                    strcat(err_msg, dep->name);
                    strcat(err_msg, " failed, aborting ");
                    strcat(err_msg, s->name);
                    log_service_event("ERROR", s->name, err_msg);
                    s->state = SERVICE_FAILED;
                    return -1;
                }
            }
        }
    }

    s->state = SERVICE_STARTING;
    char start_msg[128];
    strcpy(start_msg, "starting ");
    strcat(start_msg, s->name);
    log_service_event("INFO", s->name, start_msg);

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    s->last_start_time = (uint64_t)ts.tv_sec;
    if (s->start_time == 0) s->start_time = s->last_start_time;

    char *argv[] = {s->command, NULL};
    int pid = spawn(s->command, argv);
    if (pid > 0) {
        s->pid = pid;
        s->state = SERVICE_RUNNING;

        char run_msg[128];
        char pid_s[16];
        int_to_str(pid, pid_s);
        strcpy(run_msg, s->name);
        strcat(run_msg, " running pid=");
        strcat(run_msg, pid_s);
        log_service_event("INFO", s->name, run_msg);
        return 0;
    } else {
        s->state = SERVICE_FAILED;
        char fail_msg[128];
        strcpy(fail_msg, "failed to start ");
        strcat(fail_msg, s->name);
        log_service_event("ERROR", s->name, fail_msg);
        return -1;
    }
}

static void restart_service(service_t *s) {
    if (!s) return;
    char rst_msg[128];
    strcpy(rst_msg, "restarting ");
    strcat(rst_msg, s->name);
    log_service_event("INFO", s->name, rst_msg);

    if (s->pid > 0) {
        stop_service(s);
        sleep(100);
    }
    s->restart_count++;
    start_service(s);
}

static void register_service_full(const char *name, const char *cmd, restart_policy_t policy, const char *dep) {
    if (num_services >= MAX_SERVICES) return;
    service_t *s = &services[num_services++];
    memset(s, 0, sizeof(service_t));
    strncpy(s->name, name, sizeof(s->name) - 1);
    strncpy(s->command, cmd, sizeof(s->command) - 1);
    s->restart_policy = policy;
    s->state = SERVICE_STOPPED;
    if (dep && dep[0]) {
        strncpy(s->depends, dep, sizeof(s->depends) - 1);
    }
}

static void load_init_config(void) {
    num_services = 0;
    int fd = open("/etc/init.conf", O_RDONLY, 0);
    if (fd >= 0) {
        char buf[2048];
        int n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n > 0) {
            buf[n] = '\0';
            char *line = buf;
            while (*line) {
                while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n') line++;
                if (!*line) break;

                char *line_end = line;
                while (*line_end && *line_end != '\r' && *line_end != '\n') line_end++;
                char saved_char = *line_end;
                *line_end = '\0';

                if (*line != '#') {
                    char *tokens[8];
                    int tcount = 0;
                    char *p = line;
                    while (*p && tcount < 8) {
                        while (*p == ' ' || *p == '\t') p++;
                        if (!*p) break;
                        tokens[tcount++] = p;
                        while (*p && *p != ' ' && *p != '\t') p++;
                        if (*p) {
                            *p = '\0';
                            p++;
                        }
                    }

                    if (tcount >= 3 && strcmp(tokens[0], "service") == 0) {
                        const char *s_name = tokens[1];
                        const char *s_cmd = tokens[2];
                        restart_policy_t s_pol = RESTART_ALWAYS;
                        char s_dep[32] = {0};

                        for (int i = 3; i < tcount; i++) {
                            if (strncmp(tokens[i], "restart=", 8) == 0) {
                                const char *pol_val = tokens[i] + 8;
                                if (strcmp(pol_val, "never") == 0) s_pol = RESTART_NEVER;
                                else if (strcmp(pol_val, "on-failure") == 0) s_pol = RESTART_ON_FAILURE;
                                else if (strcmp(pol_val, "always") == 0) s_pol = RESTART_ALWAYS;
                            } else if (strncmp(tokens[i], "depends=", 8) == 0) {
                                const char *dep_val = tokens[i] + 8;
                                strncpy(s_dep, dep_val, sizeof(s_dep) - 1);
                            }
                        }

                        register_service_full(s_name, s_cmd, s_pol, s_dep);
                    }
                }

                if (saved_char == '\0') break;
                line = line_end + 1;
            }
        }
    }

    /* Fallback default configuration if /etc/init.conf is missing or empty */
    if (num_services == 0) {
        register_service_full("loggerd", "/sbin/loggerd", RESTART_ALWAYS, "");
        register_service_full("ttyd", "/sbin/ttyd", RESTART_ALWAYS, "loggerd");
        register_service_full("netd", "/sbin/netd", RESTART_ON_FAILURE, "");
        register_service_full("logind", "/sbin/logind", RESTART_ALWAYS, "ttyd");
    }
}

static void reap_children(void) {
    int status = 0;
    int pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        service_t *s = find_service_by_pid(pid);
        if (s) {
            s->exit_status = status;
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            s->last_exit_time = (uint64_t)ts.tv_sec;
            s->pid = 0;

            char exit_msg[128];
            char stat_s[16];
            int_to_str(status, stat_s);
            strcpy(exit_msg, s->name);
            strcat(exit_msg, " exited status=");
            strcat(exit_msg, stat_s);
            log_service_event("ERROR", s->name, exit_msg);

            bool should_restart = false;
            if (s->state != SERVICE_STOPPED && s->state != SERVICE_STOPPING) {
                if (s->restart_policy == RESTART_ALWAYS) {
                    should_restart = true;
                } else if (s->restart_policy == RESTART_ON_FAILURE && status != 0) {
                    should_restart = true;
                }
            }

            if (should_restart) {
                /* Reset crash counter if service had run stably for > 30s */
                if (s->last_exit_time > s->last_start_time + 30) {
                    s->restart_count = 0;
                }

                if (s->restart_count < 5) {
                    s->restart_count++;
                    char rst_msg[128];
                    char cnt_s[16];
                    int_to_str(s->restart_count, cnt_s);
                    strcpy(rst_msg, "restarting ");
                    strcat(rst_msg, s->name);
                    strcat(rst_msg, " (attempt ");
                    strcat(rst_msg, cnt_s);
                    strcat(rst_msg, "/5)");
                    log_service_event("INFO", s->name, rst_msg);

                    start_service(s);
                } else {
                    char limit_msg[128];
                    strcpy(limit_msg, s->name);
                    strcat(limit_msg, " restart limit reached");
                    log_service_event("ERROR", s->name, limit_msg);

                    s->state = SERVICE_FAILED;
                }
            } else {
                s->state = SERVICE_STOPPED;
            }
        } else {
            printf("[INIT] Reaped child PID %d (status %d)\n", pid, status);
        }
    }
}

static void shutdown_system(int reboot_cmd) {
    if (system_init_state != INIT_RUNNING) return;
    system_init_state = INIT_SHUTTING_DOWN;

    printf("\n[INIT] Controlled shutdown sequence initiated\n");
    log_service_event("WARN", "init", "system shutdown initiated");

    /* 1. Stop all services in reverse dependency order */
    for (int i = (int)num_services - 1; i >= 0; i--) {
        if (services[i].state == SERVICE_RUNNING || services[i].pid > 0) {
            stop_service(&services[i]);
        }
    }

    /* 2. Terminate active sessions and background processes */
    proc_info_t procs[32];
    int nprocs = getprocs(procs, 32);
    if (nprocs > 0) {
        for (int i = 0; i < nprocs; i++) {
            if (procs[i].pid > 1) {
                kill(procs[i].pid, SIGTERM);
            }
        }
        sleep(100);
        for (int i = 0; i < nprocs; i++) {
            if (procs[i].pid > 1) {
                kill(procs[i].pid, SIGKILL);
            }
        }
    }

    /* 3. Clean up PID files and runtime sockets */
    unlink("/run/loggerd.pid");
    unlink("/run/netd.pid");
    unlink("/run/ttyd.pid");
    unlink("/run/logind.pid");
    unlink("/run/loggerd.sock");
    unlink("/run/logger.sock");
    unlink("/run/netd.sock");
    unlink("/run/init.sock");

    /* 4. Flush filesystems */
    printf("[INIT] Syncing filesystems...\n");
    sync();
    printf("[ OK ] Filesystems synchronized\n");

    /* 5. Transition to HALTED state and power off / reboot */
    system_init_state = INIT_HALTED;
    printf("[INIT] System halted.\n");

    reboot(reboot_cmd);

    while (1) {
        sleep(1000);
    }
}

static void handle_ipc_command(int client_fd) {
    reap_children();

    char req[128];
    int64_t n = recv(client_fd, req, sizeof(req) - 1, 0);
    if (n <= 0) {
        close(client_fd);
        return;
    }
    req[n] = '\0';

    /* Parse commands: list, start <name>, stop <name>, restart <name>, status <name>, shutdown, reboot */
    if (strncmp(req, "shutdown", 8) == 0) {
        send(client_fd, "OK\n", 3, 0);
        close(client_fd);
        shutdown_system(REBOOT_CMD_POWEROFF);
        return;
    } else if (strncmp(req, "reboot", 6) == 0) {
        send(client_fd, "OK\n", 3, 0);
        close(client_fd);
        shutdown_system(REBOOT_CMD_REBOOT);
        return;
    } else if (strncmp(req, "list", 4) == 0) {
        char resp[1024];
        resp[0] = '\0';
        strcat(resp, "SERVICE     PID     STATE       RESTARTS\n");
        strcat(resp, "----------------------------------------\n");
        for (size_t i = 0; i < num_services; i++) {
            char line[128];
            char pid_buf[16];
            int p = services[i].pid;
            if (p <= 0) {
                strcpy(pid_buf, "-");
            } else {
                int_to_str(p, pid_buf);
            }

            char r_buf[16];
            int_to_str(services[i].restart_count, r_buf);

            strcpy(line, services[i].name);
            while (strlen(line) < 12) strcat(line, " ");
            strcat(line, pid_buf);
            while (strlen(line) < 20) strcat(line, " ");
            strcat(line, state_to_str(services[i].state));
            while (strlen(line) < 32) strcat(line, " ");
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
            char p_buf[16];
            char r_buf[16];
            int_to_str(s->pid, p_buf);
            int_to_str(s->restart_count, r_buf);

            resp[0] = '\0';
            strcat(resp, "SERVICE:  "); strcat(resp, s->name); strcat(resp, "\n");
            strcat(resp, "COMMAND:  "); strcat(resp, s->command); strcat(resp, "\n");
            strcat(resp, "STATE:    "); strcat(resp, state_to_str(s->state)); strcat(resp, "\n");
            strcat(resp, "PID:      "); strcat(resp, p_buf); strcat(resp, "\n");
            strcat(resp, "RESTARTS: "); strcat(resp, r_buf); strcat(resp, "\n");
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

    /* Create /run/init.sock for service control */
    init_sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (init_sock_fd >= 0) {
        struct sockaddr_un sun;
        sun.sun_family = AF_UNIX;
        strcpy(sun.sun_path, "/run/init.sock");
        bind(init_sock_fd, (struct sockaddr *)&sun, sizeof(sun));
        listen(init_sock_fd, 5);
    }

    /* Start services sequentially in dependency order */
    for (size_t i = 0; i < num_services; i++) {
        start_service(&services[i]);
        sleep(100);
    }

    printf("\n[INIT] All configured services initialized.\n");

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
        reap_children();

        sleep(100);
    }

    return 0;
}
