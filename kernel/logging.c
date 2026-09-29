/* =============================================================================
 * Nyota OS — Kernel Log Ring Buffer Subsystem
 * Bounded circular buffer of log records for userspace loggerd consumption.
 * =========================================================================== */

#include "kernel.h"
#include "time/clock.h"
#include "memory.h"
#include "serial.h"

#define KLOG_CAPACITY   128
#define KLOG_MSG_MAX    128

typedef struct {
    uint64_t timestamp_ms;
    uint32_t level;
    char message[KLOG_MSG_MAX];
} klog_entry_t;

static klog_entry_t klog_buffer[KLOG_CAPACITY];
static size_t klog_head = 0;
static size_t klog_tail = 0;
static size_t klog_count = 0;

void klog_init(void) {
    memset(klog_buffer, 0, sizeof(klog_buffer));
    klog_head = 0;
    klog_tail = 0;
    klog_count = 0;
}

void klog_write(uint32_t level, const char *msg) {
    if (!msg) return;

    klog_entry_t *entry = &klog_buffer[klog_tail];
    entry->timestamp_ms = clock_get_monotonic_ms();
    entry->level = level;

    size_t len = strlen(msg);
    if (len >= KLOG_MSG_MAX) len = KLOG_MSG_MAX - 1;
    memcpy(entry->message, msg, len);
    entry->message[len] = '\0';

    klog_tail = (klog_tail + 1) % KLOG_CAPACITY;
    if (klog_count < KLOG_CAPACITY) {
        klog_count++;
    } else {
        /* Overwrite oldest entry */
        klog_head = (klog_head + 1) % KLOG_CAPACITY;
    }
}

int klog_read_entries(char *buf, size_t buf_sz, bool clear) {
    if (!buf || buf_sz == 0) return 0;

    size_t written = 0;
    buf[0] = '\0';

    size_t idx = klog_head;
    size_t items = klog_count;

    for (size_t i = 0; i < items; i++) {
        klog_entry_t *e = &klog_buffer[idx];

        const char *lvl_str = "INFO";
        if (e->level == 0) lvl_str = "DEBUG";
        else if (e->level == 2) lvl_str = "WARN";
        else if (e->level == 3) lvl_str = "ERROR";
        else if (e->level >= 4) lvl_str = "CRIT";

        /* Format: "[sec.ms] [LEVEL] msg\n" */
        char line[256];
        uint32_t sec = (uint32_t)(e->timestamp_ms / 1000);
        uint32_t ms = (uint32_t)(e->timestamp_ms % 1000);

        int pos = 0;
        line[pos++] = '[';
        /* format sec */
        if (sec == 0) {
            line[pos++] = '0';
        } else {
            char tmp[16];
            int ti = 0;
            uint32_t s = sec;
            while (s > 0) { tmp[ti++] = '0' + (s % 10); s /= 10; }
            while (ti > 0) { line[pos++] = tmp[--ti]; }
        }
        line[pos++] = '.';
        line[pos++] = '0' + ((ms / 100) % 10);
        line[pos++] = '0' + ((ms / 10) % 10);
        line[pos++] = '0' + (ms % 10);
        line[pos++] = ']';
        line[pos++] = ' ';
        line[pos++] = '[';
        for (const char *p = lvl_str; *p; p++) line[pos++] = *p;
        line[pos++] = ']';
        line[pos++] = ' ';
        for (const char *p = e->message; *p && pos < 250; p++) line[pos++] = *p;
        line[pos++] = '\n';
        line[pos] = '\0';

        size_t l_len = (size_t)pos;
        if (written + l_len >= buf_sz) {
            break;
        }

        memcpy(buf + written, line, l_len);
        written += l_len;
        buf[written] = '\0';

        idx = (idx + 1) % KLOG_CAPACITY;
    }

    if (clear) {
        klog_head = 0;
        klog_tail = 0;
        klog_count = 0;
    }

    return (int)written;
}
