/* =============================================================================
 * Nyota OS — Userspace Interactive Shell (/bin/sh)
 * Runs in Ring 3, parses commands, pipelines (|), redirections (>, <),
 * background jobs (&), and invokes programs via spawn2/waitpid syscalls.
 * =========================================================================== */

#include "libnyota.h"

#define SH_LINE_MAX 256
#define SH_MAX_ARGS 16

static int bg_job_counter = 0;

static void print_help(void) {
    puts("Available commands:");
    puts("  help                - Show this help list");
    puts("  ls [dir]            - List directory contents");
    puts("  cat <file>          - Display contents of a file");
    puts("  echo [args...]      - Print text arguments");
    puts("  ps [tree]           - Display active processes and hierarchy");
    puts("  pwd                 - Print working directory");
    puts("  clear               - Clear terminal screen");
    puts("  secinfo             - Display kernel security & isolation status");
    puts("  ipctest             - Run IPC pipe & shared memory verification");
    puts("  memtest             - Run memory isolation and fault tests");
    puts("  kill [-sig] <pid>   - Send signal to process");
    puts("  stressproc [n]      - Stress test processes and IPC");
    puts("  crash               - Deliberately trigger userspace fault (#PF)");
    puts("  ifconfig            - Display network interfaces");
    puts("  ping <ip>           - Send ICMP echo requests");
    puts("  netstat             - Display network socket status");
    puts("  nslookup <host>     - Query DNS for hostname");
    puts("  netcat <ip> <port>  - Connect to TCP server");
    puts("  echo-server [port]  - Start TCP echo server");
    puts("  exit                - Exit the shell");
    puts("\nShell features: pipelines (cmd1 | cmd2), background (cmd &), redirection (>, <)");
}

static void cmd_ls(const char *path) {
    const char *target = (path && path[0]) ? path : "/";
    int fd = open(target, O_RDONLY);
    if (fd < 0) {
        printf("ls: cannot access '%s': No such directory\n", target);
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

    if (cmd[0] == '/') {
        strncpy(out_path, cmd, out_max - 1);
        out_path[out_max - 1] = '\0';
        stat_t st;
        return (stat(out_path, &st) == 0);
    }

    strcpy(out_path, "/bin/");
    strncpy(out_path + 5, cmd, out_max - 6);
    out_path[out_max - 1] = '\0';
    stat_t st;
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

static void cmd_run(const char *path, char **args) {
    bool background = false;
    int in_fd = -1;
    int out_fd = -1;

    int argc = 0;
    while (args && args[argc]) argc++;

    /* Check for trailing background operator '&' */
    if (argc > 0 && strcmp(args[argc - 1], "&") == 0) {
        background = true;
        args[argc - 1] = NULL;
        argc--;
    }

    /* Check for redirection operators */
    for (int i = 0; i < argc; i++) {
        if (strcmp(args[i], ">") == 0) {
            if (i + 1 < argc) {
                const char *outfile = args[i + 1];
                out_fd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC);
                if (out_fd < 0) {
                    printf("sh: cannot open '%s' for output\n", outfile);
                    return;
                }
                args[i] = NULL;
                argc = i;
                break;
            }
        } else if (strcmp(args[i], "<") == 0) {
            if (i + 1 < argc) {
                const char *infile = args[i + 1];
                in_fd = open(infile, O_RDONLY);
                if (in_fd < 0) {
                    printf("sh: cannot open '%s' for input\n", infile);
                    return;
                }
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
        printf("error: cannot execute '%s'\n", path);
        return;
    }

    if (background) {
        bg_job_counter++;
        printf("[%d] %d\n", bg_job_counter, pid);
    } else {
        int status = 0;
        waitpid(pid, &status);
    }
}

static void execute_pipeline(char *cmd1_str, char *cmd2_str) {
    char *tokens1[SH_MAX_ARGS];
    char *tokens2[SH_MAX_ARGS];

    tokenize(cmd1_str, tokens1, SH_MAX_ARGS);
    tokenize(cmd2_str, tokens2, SH_MAX_ARGS);

    if (!tokens1[0] || !tokens2[0]) {
        puts("sh: invalid pipeline syntax");
        return;
    }

    char bin1[64];
    char bin2[64];

    if (!resolve_binary(tokens1[0], bin1, sizeof(bin1))) {
        printf("nyota: command not found: %s\n", tokens1[0]);
        return;
    }
    if (!resolve_binary(tokens2[0], bin2, sizeof(bin2))) {
        printf("nyota: command not found: %s\n", tokens2[0]);
        return;
    }

    int pipefds[2];
    if (pipe(pipefds) < 0) {
        puts("sh: failed to create pipeline pipe");
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

    char line[SH_LINE_MAX];

    while (1) {
        printf("nyota$ ");

        int len = getline(line, sizeof(line));
        if (len < 0) break;

        /* Trim trailing whitespace */
        while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;

        /* Check for pipeline '|' */
        char *pipe_char = NULL;
        for (int i = 0; i < len; i++) {
            if (line[i] == '|') {
                pipe_char = &line[i];
                break;
            }
        }

        if (pipe_char) {
            *pipe_char = '\0';
            char *cmd1_str = line;
            char *cmd2_str = pipe_char + 1;
            execute_pipeline(cmd1_str, cmd2_str);
            continue;
        }

        /* Parse tokens */
        char *tokens[SH_MAX_ARGS];
        tokenize(line, tokens, SH_MAX_ARGS);

        if (!tokens[0]) continue;

        const char *cmd = tokens[0];

        if (strcmp(cmd, "help") == 0) {
            print_help();
        } else if (strcmp(cmd, "clear") == 0) {
            for (int i = 0; i < 25; i++) putchar('\n');
        } else if (strcmp(cmd, "pwd") == 0) {
            puts("/");
        } else if (strcmp(cmd, "exit") == 0) {
            puts("Exiting Nyota shell.");
            break;
        } else {
            char bin_path[64];
            if (resolve_binary(cmd, bin_path, sizeof(bin_path))) {
                cmd_run(bin_path, tokens);
            } else if (strcmp(cmd, "ls") == 0) {
                cmd_ls(tokens[1] ? tokens[1] : "/");
            } else if (strcmp(cmd, "cat") == 0) {
                if (tokens[1]) cmd_cat(tokens[1]);
                else puts("Usage: cat <filename>");
            } else if (strcmp(cmd, "echo") == 0) {
                int i = 1;
                while (tokens[i]) {
                    printf("%s%s", tokens[i], tokens[i + 1] ? " " : "");
                    i++;
                }
                putchar('\n');
            } else {
                printf("nyota: command not found: %s\n", cmd);
            }
        }
    }

    return 0;
}
