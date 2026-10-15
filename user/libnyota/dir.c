/* =============================================================================
 * Nyota OS — POSIX Directory Streaming APIs (dir.c)
 * Provides opendir, readdir, closedir, and rewinddir using getdents().
 * =========================================================================== */

#include "libnyota.h"

DIR *opendir(const char *name) {
    if (!name) return NULL;

    int fd = open(name, O_RDONLY);
    if (fd < 0) {
        return NULL;
    }

    stat_t st;
    if (stat(name, &st) == 0) {
        /* In NyotaFS, NYOTA_MODE_DIR is 0x4000 or file type 2 */
        if ((st.mode & 0xF000) != 0 && (st.mode & 0x4000) == 0) {
            close(fd);
            return NULL;
        }
    }

    DIR *d = (DIR *)malloc(sizeof(DIR));
    if (!d) {
        close(fd);
        return NULL;
    }

    d->fd = fd;
    memset(&d->cur_entry, 0, sizeof(d->cur_entry));
    return d;
}

dirent_t *readdir(DIR *dirp) {
    if (!dirp || dirp->fd < 0) return NULL;

    int ret = getdents(dirp->fd, &dirp->cur_entry);
    if (ret <= 0) {
        return NULL;
    }

    return &dirp->cur_entry;
}

int closedir(DIR *dirp) {
    if (!dirp) return -1;

    int ret = close(dirp->fd);
    free(dirp);
    return ret;
}

void rewinddir(DIR *dirp) {
    if (dirp && dirp->fd >= 0) {
        seek(dirp->fd, 0, SEEK_SET);
    }
}
