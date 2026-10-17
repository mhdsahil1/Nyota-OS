/* =============================================================================
 * Nyota OS — Native Package Manager Utility (/bin/nyotapkg)
 * Handles installation, inspection, listing, and removal of Nyota Package (.npk)
 * archives with strict path traversal defenses and metadata validation.
 * =========================================================================== */

#include "libnyota.h"

#define NPK_MAGIC       "NPK1"
#define NPK_NAME_MAX    32
#define NPK_VER_MAX     16
#define NPK_DESC_MAX    64
#define NPK_AUTH_MAX    32
#define NPK_PATH_MAX    64
#define NPK_MAX_FILES   128
#define PKG_DB_DIR      "/var/lib/pkg/installed"

typedef struct __attribute__((packed)) {
    char magic[4];                  /* "NPK1" */
    char name[NPK_NAME_MAX];        /* Package identifier */
    char version[NPK_VER_MAX];      /* Semantic version */
    char desc[NPK_DESC_MAX];        /* Summary description */
    char author[NPK_AUTH_MAX];      /* Maintainer / Author */
    uint32_t file_count;            /* Number of archived files */
} npk_header_t;

typedef struct __attribute__((packed)) {
    char path[NPK_PATH_MAX];        /* Relative path e.g. "bin/foo" */
    uint32_t mode;                  /* File mode/permissions */
    uint32_t size;                  /* Payload size in bytes */
} npk_file_header_t;

/* ── Validation Helpers ───────────────────────────────────────────────────── */

static int is_valid_name(const char *name) {
    if (!name || !*name) return 0;
    size_t len = strlen(name);
    if (len >= NPK_NAME_MAX) return 0;
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (!isalnum((unsigned char)c) && c != '-' && c != '_') {
            return 0;
        }
    }
    return 1;
}

static int is_valid_version(const char *ver) {
    if (!ver || !*ver) return 0;
    size_t len = strlen(ver);
    if (len >= NPK_VER_MAX) return 0;
    for (size_t i = 0; i < len; i++) {
        char c = ver[i];
        if (!isalnum((unsigned char)c) && c != '.' && c != '-') {
            return 0;
        }
    }
    return 1;
}

static int is_safe_path(const char *path) {
    if (!path || !*path) return 0;
    size_t len = strlen(path);
    if (len >= NPK_PATH_MAX) return 0;

    /* Reject leading slashes and drive letters */
    if (path[0] == '/' || path[0] == '\\' || strchr(path, ':')) {
        return 0;
    }

    /* Reject path traversal tokens */
    if (strstr(path, "..")) {
        return 0;
    }

    /* Enforce approved root directory destinations */
    if (strncmp(path, "bin/", 4) != 0 &&
        strncmp(path, "etc/", 4) != 0 &&
        strncmp(path, "share/", 6) != 0 &&
        strncmp(path, "lib/", 4) != 0 &&
        strncmp(path, "usr/", 4) != 0) {
        return 0;
    }

    return 1;
}

/* ── Command: info ────────────────────────────────────────────────────────── */

static int cmd_info(const char *pkg_path) {
    int fd = open(pkg_path, O_RDONLY);
    if (fd < 0) {
        printf("nyotapkg: cannot open '%s': No such file\n", pkg_path);
        return 1;
    }

    npk_header_t hdr;
    if (read(fd, &hdr, sizeof(hdr)) != sizeof(hdr)) {
        printf("nyotapkg: truncated header in '%s'\n", pkg_path);
        close(fd);
        return 1;
    }

    if (memcmp(hdr.magic, NPK_MAGIC, 4) != 0) {
        printf("nyotapkg: invalid package format in '%s' (magic mismatch)\n", pkg_path);
        close(fd);
        return 1;
    }

    hdr.name[NPK_NAME_MAX - 1] = '\0';
    hdr.version[NPK_VER_MAX - 1] = '\0';
    hdr.desc[NPK_DESC_MAX - 1] = '\0';
    hdr.author[NPK_AUTH_MAX - 1] = '\0';

    printf("\nPackage Metadata: %s\n", pkg_path);
    printf("  Name:        %s\n", hdr.name);
    printf("  Version:     %s\n", hdr.version);
    printf("  Author:      %s\n", hdr.author);
    printf("  Description: %s\n", hdr.desc);
    printf("  Files:       %u\n\n", hdr.file_count);

    if (hdr.file_count > NPK_MAX_FILES) {
        printf("nyotapkg: corrupt package (file count %u exceeds limit)\n", hdr.file_count);
        close(fd);
        return 1;
    }

    printf("Archived Files:\n");
    for (uint32_t i = 0; i < hdr.file_count; i++) {
        npk_file_header_t fhdr;
        if (read(fd, &fhdr, sizeof(fhdr)) != sizeof(fhdr)) {
            printf("  [truncated file header]\n");
            break;
        }
        fhdr.path[NPK_PATH_MAX - 1] = '\0';
        printf("  - %-28s %6u bytes (mode 0%o)%s\n",
               fhdr.path, fhdr.size, fhdr.mode,
               is_safe_path(fhdr.path) ? "" : " [UNSAFE]");
        seek(fd, fhdr.size, SEEK_CUR);
    }
    printf("\n");

    close(fd);
    return 0;
}

/* ── Command: install ─────────────────────────────────────────────────────── */

static int cmd_install(const char *pkg_path) {
    int fd = open(pkg_path, O_RDONLY);
    if (fd < 0) {
        printf("nyotapkg: cannot open '%s': No such file\n", pkg_path);
        return 1;
    }

    npk_header_t hdr;
    if (read(fd, &hdr, sizeof(hdr)) != sizeof(hdr)) {
        printf("nyotapkg: truncated header in '%s'\n", pkg_path);
        close(fd);
        return 1;
    }

    if (memcmp(hdr.magic, NPK_MAGIC, 4) != 0) {
        printf("nyotapkg: invalid package format in '%s'\n", pkg_path);
        close(fd);
        return 1;
    }

    hdr.name[NPK_NAME_MAX - 1] = '\0';
    hdr.version[NPK_VER_MAX - 1] = '\0';
    hdr.desc[NPK_DESC_MAX - 1] = '\0';
    hdr.author[NPK_AUTH_MAX - 1] = '\0';

    if (!is_valid_name(hdr.name)) {
        printf("nyotapkg: security error: invalid package name '%s'\n", hdr.name);
        close(fd);
        return 1;
    }

    if (!is_valid_version(hdr.version)) {
        printf("nyotapkg: security error: invalid version string '%s'\n", hdr.version);
        close(fd);
        return 1;
    }

    if (hdr.file_count == 0 || hdr.file_count > NPK_MAX_FILES) {
        printf("nyotapkg: error: invalid file count %u\n", hdr.file_count);
        close(fd);
        return 1;
    }

    printf("Installing package '%s' v%s...\n", hdr.name, hdr.version);

    /* Allocate buffer for recorded installed paths */
    char installed_paths[NPK_MAX_FILES][NPK_PATH_MAX];
    uint32_t installed_count = 0;

    for (uint32_t i = 0; i < hdr.file_count; i++) {
        npk_file_header_t fhdr;
        if (read(fd, &fhdr, sizeof(fhdr)) != sizeof(fhdr)) {
            printf("nyotapkg: error: truncated file header\n");
            close(fd);
            return 1;
        }
        fhdr.path[NPK_PATH_MAX - 1] = '\0';

        if (!is_safe_path(fhdr.path)) {
            printf("nyotapkg: security error: rejected unsafe path traversal: '%s'\n", fhdr.path);
            close(fd);
            return 1;
        }

        /* Target path in root fs: e.g. "/bin/devdemo" */
        char target_path[128];
        snprintf(target_path, sizeof(target_path), "/%s", fhdr.path);

        /* Write payload */
        int out_fd = open(target_path, O_CREAT | O_WRONLY | O_TRUNC);
        if (out_fd < 0) {
            printf("nyotapkg: error: failed to create target file '%s'\n", target_path);
            close(fd);
            return 1;
        }

        uint32_t remaining = fhdr.size;
        char chunk[512];
        while (remaining > 0) {
            size_t to_read = (remaining > sizeof(chunk)) ? sizeof(chunk) : remaining;
            int64_t n = read(fd, chunk, to_read);
            if (n <= 0) {
                printf("nyotapkg: error: unexpected EOF extracting '%s'\n", fhdr.path);
                close(out_fd);
                close(fd);
                return 1;
            }
            write(out_fd, chunk, (size_t)n);
            remaining -= (uint32_t)n;
        }
        close(out_fd);

        /* Apply file mode if supported */
        if (fhdr.mode != 0) {
            chmod(target_path, fhdr.mode);
        }

        printf("  + Extracted: %s (%u bytes)\n", target_path, fhdr.size);
        strncpy(installed_paths[installed_count++], fhdr.path, NPK_PATH_MAX - 1);
    }
    close(fd);

    /* Write manifest to /var/lib/pkg/installed/<name>.list */
    char manifest_path[128];
    snprintf(manifest_path, sizeof(manifest_path), "%s/%s.list", PKG_DB_DIR, hdr.name);

    int mfd = open(manifest_path, O_CREAT | O_WRONLY | O_TRUNC);
    if (mfd >= 0) {
        char line[256];
        snprintf(line, sizeof(line), "NAME=%s\nVERSION=%s\nDESC=%s\nAUTHOR=%s\n",
                 hdr.name, hdr.version, hdr.desc, hdr.author);
        write(mfd, line, strlen(line));

        for (uint32_t i = 0; i < installed_count; i++) {
            snprintf(line, sizeof(line), "FILE=%s\n", installed_paths[i]);
            write(mfd, line, strlen(line));
        }
        close(mfd);
    } else {
        printf("nyotapkg: warning: could not write manifest to '%s'\n", manifest_path);
    }

    printf("Successfully installed package '%s' v%s (%u files).\n",
           hdr.name, hdr.version, installed_count);
    return 0;
}

/* ── Command: list ────────────────────────────────────────────────────────── */

static int cmd_list(void) {
    DIR *dir = opendir(PKG_DB_DIR);
    if (!dir) {
        printf("No packages currently installed.\n");
        return 0;
    }

    printf("\nInstalled Nyota Packages:\n");
    printf("  %-16s %-12s %s\n", "PACKAGE", "VERSION", "DESCRIPTION");
    printf("  ------------------------------------------------------------\n");

    int count = 0;
    dirent_t *ent = NULL;
    while ((ent = readdir(dir)) != NULL) {
        char *dot = strstr(ent->d_name, ".list");
        if (!dot || strcmp(dot, ".list") != 0) continue;

        char path[128];
        snprintf(path, sizeof(path), "%s/%s", PKG_DB_DIR, ent->d_name);

        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;

        char buf[512];
        int64_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n <= 0) continue;
        buf[n] = '\0';

        char name[32] = {0};
        char ver[16] = {0};
        char desc[64] = {0};

        /* Parse simple key=value lines */
        char *line = buf;
        while (line && *line) {
            char *next = strchr(line, '\n');
            if (next) *next = '\0';

            if (strncmp(line, "NAME=", 5) == 0) {
                strncpy(name, line + 5, sizeof(name) - 1);
            } else if (strncmp(line, "VERSION=", 8) == 0) {
                strncpy(ver, line + 8, sizeof(ver) - 1);
            } else if (strncmp(line, "DESC=", 5) == 0) {
                strncpy(desc, line + 5, sizeof(desc) - 1);
            }

            if (!next) break;
            line = next + 1;
        }

        if (name[0]) {
            printf("  %-16s %-12s %s\n", name, ver[0] ? ver : "unknown", desc[0] ? desc : "-");
            count++;
        }
    }
    closedir(dir);

    if (count == 0) {
        printf("  (none)\n");
    }
    printf("\nTotal: %d package(s)\n\n", count);
    return 0;
}

/* ── Command: remove ──────────────────────────────────────────────────────── */

static int cmd_remove(const char *pkg_name) {
    if (!is_valid_name(pkg_name)) {
        printf("nyotapkg: error: invalid package name '%s'\n", pkg_name);
        return 1;
    }

    char manifest_path[128];
    snprintf(manifest_path, sizeof(manifest_path), "%s/%s.list", PKG_DB_DIR, pkg_name);

    int fd = open(manifest_path, O_RDONLY);
    if (fd < 0) {
        printf("nyotapkg: package '%s' is not installed\n", pkg_name);
        return 1;
    }

    char buf[1024];
    int64_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n < 0) n = 0;
    buf[n] = '\0';

    printf("Removing package '%s'...\n", pkg_name);

    /* Unlink each file */
    char *line = buf;
    while (line && *line) {
        char *next = strchr(line, '\n');
        if (next) *next = '\0';

        if (strncmp(line, "FILE=", 5) == 0) {
            char *rel_path = line + 5;
            if (is_safe_path(rel_path)) {
                char target[128];
                snprintf(target, sizeof(target), "/%s", rel_path);
                int res = unlink(target);
                if (res == 0) {
                    printf("  - Removed: %s\n", target);
                } else {
                    printf("  ! Missing or unable to remove: %s\n", target);
                }
            }
        }

        if (!next) break;
        line = next + 1;
    }

    /* Remove manifest */
    unlink(manifest_path);
    printf("Successfully removed package '%s'.\n", pkg_name);
    return 0;
}

/* ── Usage / Main ─────────────────────────────────────────────────────────── */

static void print_usage(void) {
    printf("Nyota OS Package Manager (nyotapkg) v1.7.0\n");
    printf("Usage:\n");
    printf("  nyotapkg info <file.npk>     Inspect package metadata and contents\n");
    printf("  nyotapkg install <file.npk>  Validate and install native application\n");
    printf("  nyotapkg list                List all installed packages\n");
    printf("  nyotapkg remove <name>       Uninstall package and remove files\n");
    printf("  nyotapkg help                Display this help reference\n\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "info") == 0) {
        if (argc < 3) {
            printf("nyotapkg: 'info' requires a package path argument\n");
            return 1;
        }
        return cmd_info(argv[2]);
    } else if (strcmp(cmd, "install") == 0) {
        if (argc < 3) {
            printf("nyotapkg: 'install' requires a package path argument\n");
            return 1;
        }
        return cmd_install(argv[2]);
    } else if (strcmp(cmd, "list") == 0) {
        return cmd_list();
    } else if (strcmp(cmd, "remove") == 0) {
        if (argc < 3) {
            printf("nyotapkg: 'remove' requires a package name argument\n");
            return 1;
        }
        return cmd_remove(argv[2]);
    } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0) {
        print_usage();
        return 0;
    } else {
        printf("nyotapkg: unknown command '%s'. Use 'nyotapkg help'.\n", cmd);
        return 1;
    }
}
