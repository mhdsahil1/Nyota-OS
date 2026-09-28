/* =============================================================================
 * Nyota OS — User Utility: ls (/bin/ls)
 * Traverses directories and prints filenames via getdents system call.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    const char *target = (argc > 1) ? argv[1] : "/";

    int fd = open(target, O_RDONLY);
    if (fd < 0) {
        printf("ls: cannot access '%s': No such file or directory\n", target);
        return 1;
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
    return 0;
}
