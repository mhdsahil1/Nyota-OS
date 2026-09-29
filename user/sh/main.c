/* =============================================================================
 * Nyota OS — Advanced Interactive Shell (/bin/sh)
 * Supports environment variables, custom prompt, builtins (cd, pwd, export,
 * unset, env, jobs, fg, bg), background jobs, pipelines, and redirections.
 * =========================================================================== */

#include "libnyota.h"

#define SH_LINE_MAX 256
#define SH_MAX_ARGS 16
#define MAX_JOBS    16

typedef enum {
    JOB_RUNNING,
    JOB_STOPPED,
    JOB_TERMINATED
} job_state_t;

typedef struct {
    int id;
    int pid;
    job_state_t state;
    char command[64];
} job_t;

static job_t job_table[MAX_JOBS];
static int next_job_id = 1;

static void job_add(int pid, const char *cmd) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_table[i].pid == 0) {
            job_table[i].id = next_job_id++;
            job_table[i].pid = pid;
            job_table[i].state = JOB_RUNNING;
            strncpy(job_table[i].command, cmd, sizeof(job_table[i].command) - 1);
            printf("[%d] %d\n", job_table[i].id, pid);
            return;
        }
    }
}

static void cmd_jobs(void) {
    bool found = false;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_table[i].pid > 0) {
            found = true;
            const char *st = (job_table[i].state == JOB_RUNNING) ? "RUNNING" :
                             (job_table[i].state == JOB_STOPPED) ? "STOPPED" : "TERMINATED";
            printf("[%d] %s %s\n", job_table[i].id, st, job_table[i].command);
        }
    }
    if (!found) {
        printf("No active jobs.\n");
    }
}

static void cmd_fg(const char *arg) {
    int target_id = (arg != NULL) ? atoi(arg) : 1;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_table[i].pid > 0 && (job_table[i].id == target_id || arg == NULL)) {
            int pid = job_table[i].pid;
            tty_ctrl(TTY_CTRL_SET_PGRP, pid);
            kill(pid, SIGCONT);
            int status = 0;
            waitpid(pid, &status);
            job_table[i].pid = 0;
            tty_ctrl(TTY_CTRL_SET_PGRP, getpid());
            return;
        }
    }
    printf("fg: no such job\n");
}

static void cmd_bg(const char *arg) {
    int target_id = (arg != NULL) ? atoi(arg) : 1;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_table[i].pid > 0 && (job_table[i].id == target_id || arg == NULL)) {
            kill(job_table[i].pid, SIGCONT);
            job_table[i].state = JOB_RUNNING;
            printf("[%d] RUNNING %s\n", job_table[i].id, job_table[i].command);
            return;
        }
    }
    printf("bg: no such job\n");
}

static void print_prompt(void) {
    const char *user = getenv("USER");
    if (!user) user = "sahil";

    char host[32];
    int hfd = open("/etc/hostname", O_RDONLY);
    if (hfd >= 0) {
        int64_t n = read(hfd, host, sizeof(host) - 1);
        close(hfd);
        if (n > 0) {
            host[n] = '\0';
            while (n > 0 && (host[n - 1] == '\n' || host[n - 1] == '\r')) host[--n] = '\0';
        } else {
            strcpy(host, "nyota");
        }
    } else {
        strcpy(host, "nyota");
    }

    char cwd[128];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        strcpy(cwd, "/");
    }

    const char *home = getenv("HOME");
    char display_cwd[128];
    if (home && strcmp(cwd, home) == 0) {
        strcpy(display_cwd, "~");
    } else {
        strcpy(display_cwd, cwd);
    }

    printf("[%s@%s %s]$ ", user, host, display_cwd);
}

static void cmd_cd(const char *path) {
    const char *target = path;
    if (!target || target[0] == '\0' || strcmp(target, "~") == 0) {
        target = getenv("HOME");
        if (!target) target = "/";
    }

    if (chdir(target) != 0) {
        printf("cd: %s: No such directory\n", target);
    } else {
        char cwd[128];
        if (getcwd(cwd, sizeof(cwd))) {
            setenv("PWD", cwd, 1);
        }
    }
}

static void cmd_pwd(void) {
    char cwd[128];
    if (getcwd(cwd, sizeof(cwd))) {
        printf("%s\n", cwd);
    } else {
        printf("/\n");
    }
}

static void cmd_export(char *arg) {
    if (!arg) {
        /* Print all */
        if (environ) {
            for (int i = 0; environ[i] != NULL; i++) {
                printf("%s\n", environ[i]);
            }
        }
        return;
    }

    char *eq = strchr(arg, '=');
    if (eq) {
        *eq = '\0';
        const char *name = arg;
        const char *val = eq + 1;
        setenv(name, val, 1);
    }
}

static void cmd_unset(const char *name) {
    if (name) {
        unsetenv(name);
    }
}

static void cmd_env(void) {
    if (environ) {
        for (int i = 0; environ[i] != NULL; i++) {
            printf("%s\n", environ[i]);
        }
    }
}

static void cmd_ls(const char *path) {
    const char *target = (path && path[0]) ? path : ".";
    int fd = open(target, O_RDONLY);
    if (fd < 0) {
        printf("ls: cannot access '%s': No such file or directory\n", target);
        return;
    }

    dirent_t entry;
    while (getdents(fd, &entry) > 0) {
        if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0) {
            continue;
        }
        if (entry.type == 2) {
            printf("%s/\n", entry.name);
        } else {
            printf("%s\n", entry.name);
        }
    }

    close(fd);
}

static void cmd_cat(const char *path) {
    if (!path || path[0] == '\0') {
        puts("Usage: cat <filename>");
        return;
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("cat: %s: No such file\n", path);
        return;
    }

    char buf[128];
    int64_t bytes = 0;
    while ((bytes = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[bytes] = '\0';
        write(STDOUT_FILENO, buf, (size_t)bytes);
    }

    close(fd);
}

static bool resolve_binary(const char *cmd, char *out_path, size_t out_max) {
    if (!cmd || !out_path || out_max < 8) return false;

    if (cmd[0] == '/' || (cmd[0] == '.' && cmd[1] == '/')) {
        strncpy(out_path, cmd, out_max - 1);
        out_path[out_max - 1] = '\0';
        stat_t st;
        return (stat(out_path, &st) == 0);
    }

    /* Try /bin/ */
    strcpy(out_path, "/bin/");
    strncpy(out_path + 5, cmd, out_max - 6);
    out_path[out_max - 1] = '\0';
    stat_t st;
    if (stat(out_path, &st) == 0) {
        return true;
    }

    /* Try /sbin/ */
    strcpy(out_path, "/sbin/");
    strncpy(out_path + 6, cmd, out_max - 7);
    out_path[out_max - 1] = '\0';
    if (stat(out_path, &st) == 0) {
        return true;
    }

    if (strcmp(cmd, "init") == 0) {
        strcpy(out_path, "/init");
        return (stat(out_path, &st) == 0);
    }

    return false;
}

static void tokenize(char *line, char **tokens, int max_tokens) {
    int num = 0;
    char *p = line;
    while (*p && num < max_tokens - 1) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        tokens[num++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) {
            *p++ = '\0';
        }
    }
    tokens[num] = NULL;
}

static void expand_variables(char **tokens) {
    for (int i = 0; tokens && tokens[i]; i++) {
        if (tokens[i][0] == '$' && tokens[i][1] != '\0') {
            const char *val = getenv(tokens[i] + 1);
            if (val) {
                tokens[i] = (char *)val;
            } else {
                tokens[i] = "";
            }
        }
    }
}

static void cmd_run(const char *path, char **args) {
    bool background = false;
    int in_fd = -1;
    int out_fd = -1;

    int argc = 0;
    while (args && args[argc]) argc++;

    if (argc > 0 && strcmp(args[argc - 1], "&") == 0) {
        background = true;
        args[argc - 1] = NULL;
        argc--;
    }

    /* Redirection handling */
    for (int i = 0; i < argc; i++) {
        if (strcmp(args[i], ">") == 0) {
            if (i + 1 < argc) {
                out_fd = open(args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
                args[i] = NULL;
                argc = i;
                break;
            }
        } else if (strcmp(args[i], "<") == 0) {
            if (i + 1 < argc) {
                in_fd = open(args[i + 1], O_RDONLY);
                args[i] = NULL;
                argc = i;
                break;
            }
        }
    }

    int pid = spawn2(path, args, in_fd, out_fd);
    if (in_fd >= 0) close(in_fd);
    if (out_fd >= 0) close(out_fd);

    if (pid < 0) {
        printf("sh: cannot execute '%s'\n", path);
        return;
    }

    if (background) {
        job_add(pid, path);
    } else {
        tty_ctrl(TTY_CTRL_SET_PGRP, pid);
        int status = 0;
        waitpid(pid, &status);
        tty_ctrl(TTY_CTRL_SET_PGRP, getpid());
    }
}

static void execute_pipeline(char *cmd1_str, char *cmd2_str) {
    char *tokens1[SH_MAX_ARGS];
    char *tokens2[SH_MAX_ARGS];

    tokenize(cmd1_str, tokens1, SH_MAX_ARGS);
    tokenize(cmd2_str, tokens2, SH_MAX_ARGS);
    expand_variables(tokens1);
    expand_variables(tokens2);

    if (!tokens1[0] || !tokens2[0]) {
        puts("sh: invalid pipeline syntax");
        return;
    }

    char bin1[64];
    char bin2[64];

    if (!resolve_binary(tokens1[0], bin1, sizeof(bin1))) {
        printf("sh: command not found: %s\n", tokens1[0]);
        return;
    }
    if (!resolve_binary(tokens2[0], bin2, sizeof(bin2))) {
        printf("sh: command not found: %s\n", tokens2[0]);
        return;
    }

    int pipefds[2];
    if (pipe(pipefds) < 0) {
        puts("sh: failed to create pipeline");
        return;
    }

    int pid1 = spawn2(bin1, tokens1, -1, pipefds[1]);
    int pid2 = spawn2(bin2, tokens2, pipefds[0], -1);

    close(pipefds[0]);
    close(pipefds[1]);

    if (pid1 > 0) {
        int st1 = 0;
        waitpid(pid1, &st1);
    }
    if (pid2 > 0) {
        int st2 = 0;
        waitpid(pid2, &st2);
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* Initialize default environment if empty */
    if (!getenv("PATH")) setenv("PATH", "/bin:/sbin", 1);
    if (!getenv("USER")) setenv("USER", "sahil", 1);
    if (!getenv("HOME")) setenv("HOME", "/home/sahil", 1);
    if (!getenv("SHELL")) setenv("SHELL", "/bin/sh", 1);

    char cwd[128];
    if (getcwd(cwd, sizeof(cwd))) {
        setenv("PWD", cwd, 1);
    }

    memset(job_table, 0, sizeof(job_table));
    char line[SH_LINE_MAX];

    while (1) {
        print_prompt();

        int len = getline(line, sizeof(line));
        if (len < 0) break;

        while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;

        /* Pipeline */
        char *pipe_char = NULL;
        for (int i = 0; i < len; i++) {
            if (line[i] == '|') {
                pipe_char = &line[i];
                break;
            }
        }

        if (pipe_char) {
            *pipe_char = '\0';
            execute_pipeline(line, pipe_char + 1);
            continue;
        }

        char *tokens[SH_MAX_ARGS];
        tokenize(line, tokens, SH_MAX_ARGS);
        if (!tokens[0]) continue;

        expand_variables(tokens);
        const char *cmd = tokens[0];

        /* Shell Builtins */
        if (strcmp(cmd, "cd") == 0) {
            cmd_cd(tokens[1]);
        } else if (strcmp(cmd, "pwd") == 0) {
            cmd_pwd();
        } else if (strcmp(cmd, "export") == 0) {
            cmd_export(tokens[1]);
        } else if (strcmp(cmd, "unset") == 0) {
            cmd_unset(tokens[1]);
        } else if (strcmp(cmd, "env") == 0) {
            cmd_env();
        } else if (strcmp(cmd, "jobs") == 0) {
            cmd_jobs();
        } else if (strcmp(cmd, "fg") == 0) {
            cmd_fg(tokens[1]);
        } else if (strcmp(cmd, "bg") == 0) {
            cmd_bg(tokens[1]);
        } else if (strcmp(cmd, "exit") == 0) {
            break;
        } else if (strcmp(cmd, "help") == 0) {
            printf("Nyota Shell Builtins:\n");
            printf("  cd, pwd, export, unset, env, jobs, fg, bg, exit, help\n");
        } else if (strcmp(cmd, "clear") == 0) {
            for (int i = 0; i < 25; i++) putchar('\n');
        } else if (strcmp(cmd, "echo") == 0) {
            for (int i = 1; tokens[i]; i++) {
                printf("%s%s", tokens[i], tokens[i + 1] ? " " : "");
            }
            putchar('\n');
        } else if (strcmp(cmd, "ls") == 0) {
            cmd_ls(tokens[1]);
        } else if (strcmp(cmd, "cat") == 0) {
            cmd_cat(tokens[1]);
        } else {
            char bin_path[64];
            if (resolve_binary(cmd, bin_path, sizeof(bin_path))) {
                cmd_run(bin_path, tokens);
            } else {
                printf("sh: %s: command not found\n", cmd);
            }
        }
    }

    return 0;
}
