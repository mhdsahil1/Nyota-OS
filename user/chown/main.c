/* =============================================================================
 * Nyota OS — File Owner Utility (/bin/chown)
 * Changes file ownership in NyotaFS.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: chown <uid>[:<gid>] <file>\n");
        return 1;
    }

    const char *spec = argv[1];
    const char *path = argv[2];

    uint32_t uid = 0;
    uint32_t gid = 0;

    char spec_copy[64];
    strncpy(spec_copy, spec, sizeof(spec_copy) - 1);
    spec_copy[sizeof(spec_copy) - 1] = '\0';

    char *colon = strchr(spec_copy, ':');
    if (colon) {
        *colon = '\0';
        uid = (uint32_t)atoi(spec_copy);
        gid = (uint32_t)atoi(colon + 1);
    } else {
        uid = (uint32_t)atoi(spec_copy);
        stat_t st;
        if (stat(path, &st) == 0) {
            gid = st.gid;
        }
    }

    int ret = chown(path, uid, gid);
    if (ret != 0) {
        printf("chown: failed to change ownership of '%s' (err %d)\n", path, ret);
        return 1;
    }

    return 0;
}
