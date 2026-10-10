/* =============================================================================
 * Nyota OS — File Mode Utility (/bin/chmod)
 * Changes file access permissions in NyotaFS.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: chmod <octal_mode> <file>\n");
        return 1;
    }

    const char *mode_str = argv[1];
    const char *path = argv[2];

    /* Parse octal mode */
    uint32_t mode = 0;
    while (*mode_str) {
        if (*mode_str < '0' || *mode_str > '7') {
            printf("chmod: invalid octal mode '%s'\n", argv[1]);
            return 1;
        }
        mode = (mode << 3) | (*mode_str - '0');
        mode_str++;
    }

    int ret = chmod(path, mode);
    if (ret != 0) {
        printf("chmod: failed to change mode of '%s' (err %d)\n", path, ret);
        return 1;
    }

    return 0;
}
