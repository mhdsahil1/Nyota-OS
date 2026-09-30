/* =============================================================================
 * Nyota OS — Terminal & TTY Subsystem Interface
 * Canonical line editing, input/output buffering, virtual consoles, and signals.
 * =========================================================================== */

#ifndef NYOTA_DRIVERS_TTY_H
#define NYOTA_DRIVERS_TTY_H

#include "types.h"
#include "process.h"

#define MAX_TTYS            4

#define TTY_FLAG_ECHO       (1 << 0)
#define TTY_FLAG_ICANON     (1 << 1)
#define TTY_FLAG_ISIG       (1 << 2)

#define TTY_BUFFER_SIZE     1024
#define TTY_LINE_MAX        256

/* TTY IOCTLs */
#define TIOCGPGRP           0x540F
#define TIOCSPGRP           0x5410
#define TCGETS              0x5401
#define TCSETS              0x5402

typedef struct tty {
    int id;
    char name[16];

    /* Input ring buffer */
    char in_buf[TTY_BUFFER_SIZE];
    size_t in_head;
    size_t in_tail;
    size_t in_count;

    /* Canonical line editing buffer */
    char line_buf[TTY_LINE_MAX];
    size_t line_len;

    uint32_t flags;
    uint32_t foreground_pgrp;
    uint32_t session_id;
    bool     eof_pending;

    process_t *waiters;
} tty_t;

void tty_init(void);
tty_t *tty_get(int id);
tty_t *tty_get_current(void);
void tty_set_current(int id);

void tty_handle_key(char c);
int64_t tty_read(tty_t *tty, void *buf, size_t count);
int64_t tty_write(tty_t *tty, const void *buf, size_t count);
int tty_set_foreground_pgrp(tty_t *tty, uint32_t pgrp);
uint32_t tty_get_foreground_pgrp(tty_t *tty);
void tty_send_signal_to_foreground(tty_t *tty, int sig);

#endif /* NYOTA_DRIVERS_TTY_H */
