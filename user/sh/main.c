/* =============================================================================
 * Nyota OS — Userspace Interactive Shell (/bin/sh)
 * Runs in Ring 3, parses commands, executes programs via spawn/waitpid syscalls.
 * =========================================================================== */

#include "libnyota.h"

#define SH_LINE_MAX 256
#define SH_MAX_ARGS 16

static void print_help(void) {
    puts("Available commands:");
    puts("  help                - Show this help list");
    puts("  ls [dir]            - List directory contents");
    puts("  cat <file>          - Display contents of a file");
    puts("  echo [args...]      - Print text arguments");
    puts("  ps                  - Display active processes");
    puts("  pwd                 - Print working directory");
    puts("  clear               - Clear terminal screen");
    puts("  run <path> [args]   - Execute an ELF program from path");
    puts("  ifconfig            - Display network interfaces");
    puts("  ping <ip>           - Send ICMP echo requests");
    puts("  netstat             - Display network socket status");
    puts("  nslookup <host>     - Query DNS for hostname");
    puts("  netcat <ip> <port>  - Connect to TCP server");
    puts("  echo-server [port]  - Start TCP echo server");
    puts("  hello               - Run /bin/hello");
    puts("  test                - Run userspace verification test suite");
    puts("  exit                - Exit the shell");
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

static void cmd_run(const char *path, char **args) {
    bool background = false;
    int argc = 0;
    while (args && args[argc]) argc++;

    if (argc > 0 && strcmp(args[argc - 1], "&") == 0) {
        background = true;
        args[argc - 1] = NULL;
    }

    int pid = spawn(path, args);
    if (pid < 0) {
        printf("error: cannot execute '%s'\n", path);
        return;
    }

    if (background) {
        printf("[%d] started in background\n", pid);
    } else {
        int status = 0;
        waitpid(pid, &status);
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

        /* Parse tokens */
        char *tokens[SH_MAX_ARGS];
        int num_tokens = 0;

        char *p = line;
        while (*p && num_tokens < SH_MAX_ARGS - 1) {
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '\0') break;

            tokens[num_tokens++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) {
                *p++ = '\0';
            }
        }
        tokens[num_tokens] = NULL;

        if (num_tokens == 0) continue;

        const char *cmd = tokens[0];

        if (strcmp(cmd, "help") == 0) {
            print_help();
        } else if (strcmp(cmd, "clear") == 0) {
            /* Clear screen using spaces or line breaks */
            for (int i = 0; i < 25; i++) putchar('\n');
        } else if (strcmp(cmd, "pwd") == 0) {
            puts("/");
        } else if (strcmp(cmd, "echo") == 0) {
            for (int i = 1; i < num_tokens; i++) {
                printf("%s%s", tokens[i], (i == num_tokens - 1) ? "" : " ");
            }
            putchar('\n');
        } else if (strcmp(cmd, "ls") == 0) {
            cmd_ls(num_tokens > 1 ? tokens[1] : "/");
        } else if (strcmp(cmd, "cat") == 0) {
            if (num_tokens > 1) {
                cmd_cat(tokens[1]);
            } else {
                puts("Usage: cat <filename>");
            }
        } else if (strcmp(cmd, "ps") == 0) {
            /* Spawn /bin/ps or display message */
            cmd_run("/bin/ps", tokens);
        } else if (strcmp(cmd, "run") == 0) {
            if (num_tokens > 1) {
                cmd_run(tokens[1], &tokens[1]);
            } else {
                puts("Usage: run <path> [args...]");
            }
        } else if (strcmp(cmd, "exit") == 0) {
            puts("Exiting Nyota shell.");
            break;
        } else {
            /* Try executing directly from /bin/<cmd> or <cmd> */
            char bin_path[64];
            if (cmd[0] == '/') {
                strncpy(bin_path, cmd, sizeof(bin_path) - 1);
            } else {
                strcpy(bin_path, "/bin/");
                strncpy(bin_path + 5, cmd, sizeof(bin_path) - 6);
            }
            bin_path[sizeof(bin_path) - 1] = '\0';

            stat_t st;
            if (stat(bin_path, &st) == 0) {
                cmd_run(bin_path, tokens);
            } else {
                printf("nyota: command not found: %s\n", cmd);
            }
        }
    }

    return 0;
}
