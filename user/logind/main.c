/* =============================================================================
 * Nyota OS — Login Service (/sbin/logind)
 * Manages user authentication, session credentials, environment, and shell spawn.
 * =========================================================================== */

#include "libnyota.h"

typedef struct {
    char username[32];
    uint32_t uid;
    uint32_t gid;
    char home[64];
    char shell[32];
} user_record_t;

static int lookup_user(const char *name, user_record_t *rec) {
    int fd = open("/etc/passwd", O_RDONLY);
    if (fd < 0) {
        /* Default fallback */
        strcpy(rec->username, "sahil");
        rec->uid = 1000;
        rec->gid = 1000;
        strcpy(rec->home, "/home/sahil");
        strcpy(rec->shell, "/bin/sh");
        return 0;
    }

    char buf[1024];
    int64_t bytes = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (bytes <= 0) return -1;
    buf[bytes] = '\0';

    char *line = buf;
    while (*line) {
        char *eol = strchr(line, '\n');
        if (eol) *eol = '\0';

        /* Parse format: name:x:uid:gid:gecos:home:shell */
        char *p = line;
        char *uname = p;
        char *c1 = strchr(p, ':');
        if (c1) {
            *c1 = '\0';
            p = c1 + 1;
            char *c2 = strchr(p, ':');
            if (c2) {
                p = c2 + 1;
                char *c3 = strchr(p, ':');
                if (c3) {
                    *c3 = '\0';
                    int uid = atoi(p);
                    p = c3 + 1;
                    char *c4 = strchr(p, ':');
                    if (c4) {
                        *c4 = '\0';
                        int gid = atoi(p);
                        p = c4 + 1;
                        char *c5 = strchr(p, ':');
                        if (c5) {
                            p = c5 + 1;
                            char *c6 = strchr(p, ':');
                            if (c6) {
                                *c6 = '\0';
                                char *home = p;
                                char *sh = c6 + 1;

                                if (strcmp(uname, name) == 0) {
                                    strncpy(rec->username, uname, sizeof(rec->username) - 1);
                                    rec->uid = (uint32_t)uid;
                                    rec->gid = (uint32_t)gid;
                                    strncpy(rec->home, home, sizeof(rec->home) - 1);
                                    strncpy(rec->shell, sh, sizeof(rec->shell) - 1);
                                    return 0;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (!eol) break;
        line = eol + 1;
    }

    return -1;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("[logind] Starting session login daemon (PID %d)\n", getpid());

    while (1) {
        printf("\nNyota OS\n\n");
        printf("tty0 login: ");

        char username[32];
        int len = getline(username, sizeof(username));
        if (len < 0) {
            sleep(1000);
            continue;
        }

        /* Trim newline / carriage return */
        while (len > 0 && (username[len - 1] == '\n' || username[len - 1] == '\r' || username[len - 1] == ' ')) {
            username[--len] = '\0';
        }

        /* Default to sahil if empty */
        if (len == 0) {
            strcpy(username, "sahil");
        }

        printf("Password: ");
        char pass[32];
        getline(pass, sizeof(pass));

        user_record_t urec;
        if (lookup_user(username, &urec) != 0) {
            printf("Login incorrect.\n");
            sleep(1000);
            continue;
        }

        printf("\nWelcome to Nyota OS.\n\n");

        /* Establish new session and controlling terminal */
        setsid();

        /* Set up user environment */
        setenv("USER", urec.username, 1);
        setenv("HOME", urec.home, 1);
        setenv("SHELL", urec.shell, 1);
        setenv("PATH", "/bin:/sbin", 1);
        setenv("PWD", urec.home, 1);

        /* Change working directory to user home */
        chdir(urec.home);

        /* Spawn user session shell */
        char *sh_argv[] = {urec.shell, NULL};
        int child_pid = spawn(urec.shell, sh_argv);
        if (child_pid > 0) {
            /* Configure shell process group and give terminal foreground */
            setpgid(child_pid, child_pid);
            tty_ctrl(TTY_CTRL_SET_PGRP, child_pid);

            /* Track session process until termination */
            int status = 0;
            waitpid(child_pid, &status);
            printf("\n[logind] Session ended (status %d).\n", status);

            /* Session cleanup: reclaim controlling TTY and restore daemon cwd */
            tty_ctrl(TTY_CTRL_SET_PGRP, getpid());
            chdir("/");
        } else {
            printf("[logind] Failed to execute %s\n", urec.shell);
            sleep(1000);
        }
    }

    return 0;
}
