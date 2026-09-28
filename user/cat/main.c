/* =============================================================================
 * Nyota OS — User Utility: cat (/bin/cat)
 * Reads files sequentially and streams content to stdout.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        puts("Usage: cat <filename>");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            printf("cat: %s: No such file or directory\n", argv[i]);
            continue;
        }

        char buffer[256];
        int64_t bytes = 0;
        while ((bytes = read(fd, buffer, sizeof(buffer))) > 0) {
            write(STDOUT_FILENO, buffer, (size_t)bytes);
        }

        close(fd);
    }

    return 0;
}
