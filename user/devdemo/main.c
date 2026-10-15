/* =============================================================================
 * Nyota OS — Developer Platform Native Sample Application (/bin/devdemo)
 * Tests libc runtime, memory management, string utilities, directory streaming,
 * and system APIs for native C applications on Nyota OS v1.5.0.
 * =========================================================================== */

#include "libnyota.h"

static int g_pass = 0;
static int g_fail = 0;

static void assert_test(const char *name, int condition) {
    if (condition) {
        printf("  [PASS] %s\n", name);
        g_pass++;
    } else {
        printf("  [FAIL] %s\n", name);
        g_fail++;
    }
}

/* ── Test 1: malloc & 16-byte Alignment ───────────────────────────────────── */
static void test_malloc_alignment(void) {
    printf("[1/8] Testing malloc and 16-byte alignment...\n");
    void *p1 = malloc(8);
    void *p2 = malloc(48);
    void *p3 = malloc(112);

    int aligned = 1;
    if (!p1 || ((uintptr_t)p1 & 0xFULL) != 0) aligned = 0;
    if (!p2 || ((uintptr_t)p2 & 0xFULL) != 0) aligned = 0;
    if (!p3 || ((uintptr_t)p3 & 0xFULL) != 0) aligned = 0;

    assert_test("malloc 16-byte alignment", aligned);

    free(p1);
    free(p2);
    free(p3);
}

/* ── Test 2: Memory Pattern Integrity ─────────────────────────────────────── */
static void test_memory_patterns(void) {
    printf("[2/8] Testing memory pattern integrity...\n");
    size_t sizes[] = { 32, 128, 512, 1024 };
    uint8_t *ptrs[4];

    for (int i = 0; i < 4; i++) {
        ptrs[i] = (uint8_t *)malloc(sizes[i]);
        if (ptrs[i]) {
            for (size_t j = 0; j < sizes[i]; j++) {
                ptrs[i][j] = (uint8_t)((i * 37 + j) & 0xFF);
            }
        }
    }

    int valid = 1;
    for (int i = 0; i < 4; i++) {
        if (!ptrs[i]) {
            valid = 0;
            continue;
        }
        for (size_t j = 0; j < sizes[i]; j++) {
            if (ptrs[i][j] != (uint8_t)((i * 37 + j) & 0xFF)) {
                valid = 0;
                break;
            }
        }
        free(ptrs[i]);
    }

    assert_test("pattern integrity across multiple blocks", valid);
}

/* ── Test 3: calloc, realloc & Coalescing ──────────────────────────────────── */
static void test_calloc_realloc_coalesce(void) {
    printf("[3/8] Testing calloc, realloc, and free coalescing...\n");
    /* calloc */
    uint32_t *zeros = (uint32_t *)calloc(64, sizeof(uint32_t));
    int all_zero = (zeros != NULL);
    if (zeros) {
        for (int i = 0; i < 64; i++) {
            if (zeros[i] != 0) {
                all_zero = 0;
                break;
            }
        }
    }
    assert_test("calloc zero-initialization", all_zero);

    /* realloc */
    char *str = (char *)malloc(16);
    if (str) {
        strcpy(str, "Nyota");
        str = (char *)realloc(str, 64);
        int realloc_ok = (str != NULL && strcmp(str, "Nyota") == 0);
        if (str) {
            strcat(str, " OS Phase 15");
            realloc_ok = realloc_ok && (strcmp(str, "Nyota OS Phase 15") == 0);
            free(str);
        }
        assert_test("realloc growth and data preservation", realloc_ok);
    } else {
        assert_test("realloc growth and data preservation", 0);
    }

    if (zeros) free(zeros);

    /* Coalescing test: allocate 3 chunks, free first two, allocate combined */
    void *a = malloc(128);
    void *b = malloc(128);
    void *c = malloc(128);
    free(a);
    free(b);
    void *ab = malloc(200); /* Should fit in coalesced space of a and b */
    assert_test("free coalescing fit", ab != NULL);
    if (ab) free(ab);
    if (c) free(c);
}

/* ── Test 4: Large Allocations (sbrk heap expansion) ──────────────────────── */
static void test_large_allocation(void) {
    printf("[4/8] Testing large allocation (sbrk page expansion)...\n");
    size_t large_sz = 64 * 1024; /* 64 KiB (16 pages) */
    uint8_t *large = (uint8_t *)malloc(large_sz);
    int ok = (large != NULL);

    if (large) {
        /* Touch each 4K page */
        for (size_t off = 0; off < large_sz; off += 4096) {
            large[off] = 0xAA;
            if (large[off] != 0xAA) {
                ok = 0;
                break;
            }
        }
        free(large);
    }
    assert_test("64 KiB malloc, page-touch, and free", ok);
}

/* ── Test 5: String Utilities & CType ─────────────────────────────────────── */
static void test_string_and_ctype(void) {
    printf("[5/8] Testing string utilities, parsing, and ctype...\n");

    /* strdup & strndup */
    char *s1 = strdup("Developer Platform");
    char *s2 = strndup("Developer Platform", 9);
    int dup_ok = (s1 && strcmp(s1, "Developer Platform") == 0 &&
                  s2 && strcmp(s2, "Developer") == 0);
    assert_test("strdup and strndup", dup_ok);
    if (s1) free(s1);
    if (s2) free(s2);

    /* strtok_r */
    char to_tokenize[] = "bin,etc,lib,share,var";
    char *saveptr = NULL;
    char *tok = strtok_r(to_tokenize, ",", &saveptr);
    int tok_count = 0;
    while (tok) {
        tok_count++;
        tok = strtok_r(NULL, ",", &saveptr);
    }
    assert_test("strtok_r tokenization", tok_count == 5);

    /* strtol & atol */
    long v1 = strtol(" -4200", NULL, 10);
    long v2 = strtol("0x1F", NULL, 16);
    long v3 = atol("9999");
    assert_test("strtol and atol numeric parsing", v1 == -4200 && v2 == 31 && v3 == 9999);

    /* memcmp, strspn, strcspn */
    int span_ok = (strspn("12345abc", "0123456789") == 5);
    int cspan_ok = (strcspn("nyota:phase15", ":;") == 5);
    int cmp_ok = (memcmp("alpha", "alpha", 5) == 0 && memcmp("a", "b", 1) < 0);
    assert_test("memcmp, strspn, and strcspn", span_ok && cspan_ok && cmp_ok);

    /* ctype */
    int ctype_ok = (isalpha('K') && isdigit('9') && isspace(' ') &&
                    toupper('k') == 'K' && tolower('K') == 'k' &&
                    isalnum('z') && isprint('!'));
    assert_test("ctype classifications and conversions", ctype_ok);
}

/* ── Test 6: Formatted Output (vsnprintf / dprintf) ───────────────────────── */
static void test_formatted_io(void) {
    printf("[6/8] Testing snprintf and formatted I/O...\n");
    char buf[128];
    int n = snprintf(buf, sizeof(buf), "OS: %s v%d.%d.%d [0x%x]", "Nyota", 1, 5, 0, 0x15);
    int match = (strcmp(buf, "OS: Nyota v1.5.0 [0x15]") == 0 && n > 0);
    assert_test("snprintf string and hex formatting", match);
}

/* ── Test 7: POSIX Directory Streaming (opendir/readdir/closedir) ─────────── */
static void test_directory_streaming(void) {
    printf("[7/8] Testing directory streaming (/bin)...\n");
    DIR *dir = opendir("/bin");
    if (!dir) {
        assert_test("opendir(/bin)", 0);
        return;
    }

    int entry_count = 0;
    int found_sh = 0;
    dirent_t *ent = NULL;
    char first_entry[64];
    first_entry[0] = '\0';

    while ((ent = readdir(dir)) != NULL) {
        if (entry_count == 0) {
            strncpy(first_entry, ent->d_name, sizeof(first_entry) - 1);
        }
        if (strcmp(ent->d_name, "sh") == 0) {
            found_sh = 1;
        }
        entry_count++;
    }

    int stream_ok = (entry_count > 0 && found_sh);
    assert_test("readdir found entries including /bin/sh", stream_ok);

    /* rewinddir */
    rewinddir(dir);
    ent = readdir(dir);
    int rewind_ok = (ent != NULL && strcmp(ent->d_name, first_entry) == 0);
    assert_test("rewinddir returns to initial entry", rewind_ok);

    closedir(dir);
}

/* ── Test 8: File Access & I/O (access/open/write/seek/read/unlink) ────────── */
static void test_file_access_io(void) {
    printf("[8/8] Testing file access and I/O...\n");
    /* access */
    int acc_real = (access("/etc/version", F_OK) == 0 || access("/bin/sh", F_OK) == 0);
    int acc_fake = (access("/nonexistent_dev_path_xyz", F_OK) != 0);
    assert_test("access() F_OK valid and invalid", acc_real && acc_fake);

    /* temporary file creation and verification */
    const char *tmp_path = "/tmp/devdemo.tmp";
    int fd = open(tmp_path, O_CREAT | O_WRONLY | O_TRUNC);
    if (fd < 0) {
        /* Fallback if /tmp is not writable */
        tmp_path = "/devdemo.tmp";
        fd = open(tmp_path, O_CREAT | O_WRONLY | O_TRUNC);
    }

    if (fd >= 0) {
        const char *msg = "Nyota OS Native Developer Platform Test String\n";
        size_t len = strlen(msg);
        write(fd, msg, len);
        close(fd);

        fd = open(tmp_path, O_RDONLY);
        if (fd >= 0) {
            char read_buf[64];
            memset(read_buf, 0, sizeof(read_buf));
            int64_t bytes = read(fd, read_buf, sizeof(read_buf) - 1);
            close(fd);

            int match = (bytes > 0 && strncmp(read_buf, msg, (size_t)bytes) == 0);
            assert_test("file write, read, and content match", match);
        } else {
            assert_test("file write, read, and content match", 0);
        }

        unlink(tmp_path);
        assert_test("file unlink cleanup", access(tmp_path, F_OK) != 0);
    } else {
        assert_test("file write, read, and content match", 0);
        assert_test("file unlink cleanup", 0);
    }
}

/* ── Entry Point ──────────────────────────────────────────────────────────── */
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("\n===================================================\n");
    printf(" Nyota OS v1.5.0 — Developer Platform Test Suite\n");
    printf(" Native C Application Verification (/bin/devdemo)\n");
    printf("===================================================\n\n");

    test_malloc_alignment();
    test_memory_patterns();
    test_calloc_realloc_coalesce();
    test_large_allocation();
    test_string_and_ctype();
    test_formatted_io();
    test_directory_streaming();
    test_file_access_io();

    printf("\n---------------------------------------------------\n");
    printf(" Results: %d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) {
        printf(" [DEVDEMO] ALL TESTS PASSED (%d/%d)\n", g_pass, g_pass);
    } else {
        printf(" [DEVDEMO] TESTS FAILED!\n");
    }
    printf("---------------------------------------------------\n\n");

    return g_fail == 0 ? 0 : 1;
}
