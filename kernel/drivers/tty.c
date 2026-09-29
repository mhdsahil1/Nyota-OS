/* =============================================================================
 * Nyota OS — Terminal & TTY Subsystem Implementation
 * Canonical line editing, echoing, input ring buffering, and terminal signals.
 * =========================================================================== */

#include "drivers/tty.h"
#include "vga.h"
#include "serial.h"
#include "memory.h"
#include "scheduler.h"
#include "signal.h"
#include "process.h"
#include "syscall.h"

static tty_t system_ttys[MAX_TTYS];
static int current_tty_id = 0;

void tty_init(void) {
    memset(system_ttys, 0, sizeof(system_ttys));
    for (int i = 0; i < MAX_TTYS; i++) {
        system_ttys[i].id = i;
        system_ttys[i].name[0] = 't';
        system_ttys[i].name[1] = 't';
        system_ttys[i].name[2] = 'y';
        system_ttys[i].name[3] = '0' + i;
        system_ttys[i].name[4] = '\0';
        system_ttys[i].flags = TTY_FLAG_ECHO | TTY_FLAG_ICANON | TTY_FLAG_ISIG;
        system_ttys[i].foreground_pgrp = 0;
        system_ttys[i].waiters = NULL;
    }
    current_tty_id = 0;
}

tty_t *tty_get(int id) {
    if (id < 0 || id >= MAX_TTYS) return &system_ttys[0];
    return &system_ttys[id];
}

tty_t *tty_get_current(void) {
    return &system_ttys[current_tty_id];
}

void tty_set_current(int id) {
    if (id >= 0 && id < MAX_TTYS) {
        current_tty_id = id;
    }
}

int tty_set_foreground_pgrp(tty_t *tty, uint32_t pgrp) {
    if (!tty) return -1;
    tty->foreground_pgrp = pgrp;
    return 0;
}

uint32_t tty_get_foreground_pgrp(tty_t *tty) {
    if (!tty) return 0;
    return tty->foreground_pgrp;
}

static void tty_wake_waiters(tty_t *tty) {
    if (tty && tty->waiters) {
        process_t *p = tty->waiters;
        tty->waiters = NULL;
        scheduler_wake(p);
    }
}

static void tty_send_signal_to_foreground(tty_t *tty, int sig) {
    if (!tty) return;

    /* If specific pgrp is set, signal matching process */
    if (tty->foreground_pgrp > 0) {
        signal_send_pid(tty->foreground_pgrp, sig);
        return;
    }

    /* Fallback: signal the currently running user process if not PID 1 (init) */
    process_t *curr = process_get_current();
    if (curr && curr->pid > 1) {
        signal_send(curr, sig);
    }
}

void tty_handle_key(char c) {
    tty_t *tty = tty_get_current();
    if (!tty) return;

    /* Handle signals if ISIG flag is enabled */
    if (tty->flags & TTY_FLAG_ISIG) {
        if (c == 3) { /* Ctrl+C -> SIGINT */
            vga_print("^C\n");
            tty->line_len = 0;
            tty_send_signal_to_foreground(tty, SIGINT);
            return;
        }
        if (c == 26) { /* Ctrl+Z -> SIGTSTP */
            vga_print("^Z\n");
            tty->line_len = 0;
            tty_send_signal_to_foreground(tty, SIGTSTP);
            return;
        }
        if (c == 28) { /* Ctrl+\ -> SIGQUIT */
            vga_print("^\\\n");
            tty->line_len = 0;
            tty_send_signal_to_foreground(tty, SIGQUIT);
            return;
        }
    }

    /* Handle Ctrl+D (EOF) */
    if (c == 4) {
        if (tty->line_len == 0) {
            /* Signal EOF by waking waiter with 0 available bytes */
            tty_wake_waiters(tty);
            return;
        }
    }

    /* Backspace */
    if (c == '\b' || c == 0x7F) {
        if (tty->line_len > 0) {
            tty->line_len--;
            if (tty->flags & TTY_FLAG_ECHO) {
                vga_putchar('\b');
            }
        }
        return;
    }

    /* Enter / Newline */
    if (c == '\r' || c == '\n') {
        if (tty->flags & TTY_FLAG_ECHO) {
            vga_putchar('\n');
        }

        if (tty->flags & TTY_FLAG_ICANON) {
            /* Canonical mode: commit line into in_buf */
            for (size_t i = 0; i < tty->line_len; i++) {
                if (tty->in_count < TTY_BUFFER_SIZE) {
                    tty->in_buf[tty->in_tail] = tty->line_buf[i];
                    tty->in_tail = (tty->in_tail + 1) % TTY_BUFFER_SIZE;
                    tty->in_count++;
                }
            }
            if (tty->in_count < TTY_BUFFER_SIZE) {
                tty->in_buf[tty->in_tail] = '\n';
                tty->in_tail = (tty->in_tail + 1) % TTY_BUFFER_SIZE;
                tty->in_count++;
            }
            tty->line_len = 0;
            tty_wake_waiters(tty);
        } else {
            /* Raw mode */
            if (tty->in_count < TTY_BUFFER_SIZE) {
                tty->in_buf[tty->in_tail] = '\n';
                tty->in_tail = (tty->in_tail + 1) % TTY_BUFFER_SIZE;
                tty->in_count++;
                tty_wake_waiters(tty);
            }
        }
        return;
    }

    /* Printable characters */
    if (tty->flags & TTY_FLAG_ICANON) {
        if (tty->line_len < TTY_LINE_MAX - 1) {
            tty->line_buf[tty->line_len++] = c;
            if (tty->flags & TTY_FLAG_ECHO) {
                vga_putchar(c);
            }
        }
    } else {
        if (tty->in_count < TTY_BUFFER_SIZE) {
            tty->in_buf[tty->in_tail] = c;
            tty->in_tail = (tty->in_tail + 1) % TTY_BUFFER_SIZE;
            tty->in_count++;
            if (tty->flags & TTY_FLAG_ECHO) {
                vga_putchar(c);
            }
            tty_wake_waiters(tty);
        }
    }
}

int64_t tty_read(tty_t *tty, void *buf, size_t count) {
    if (!tty || !buf || count == 0) return 0;

    process_t *curr = process_get_current();

    /* Sleep if no input available */
    while (tty->in_count == 0) {
        if (!curr) return 0;

        if (curr->pending_signals & ~curr->blocked_signals) {
            return -SYS_ERR_EINTR;
        }

        tty->waiters = curr;
        curr->state = PROCESS_SLEEPING;
        scheduler_remove(curr);
        scheduler_request_reschedule();

        /* Wait until woken by tty_wake_waiters or signal */
        while (curr->state == PROCESS_SLEEPING && tty->in_count == 0) {
            __asm__ volatile ("sti; hlt");
        }

        tty->waiters = NULL;
        curr->state = PROCESS_RUNNING;

        if (tty->in_count == 0 && (curr->pending_signals & ~curr->blocked_signals)) {
            return -SYS_ERR_EINTR;
        }
    }

    char *dst = (char *)buf;
    size_t to_read = (count < tty->in_count) ? count : tty->in_count;

    for (size_t i = 0; i < to_read; i++) {
        dst[i] = tty->in_buf[tty->in_head];
        tty->in_head = (tty->in_head + 1) % TTY_BUFFER_SIZE;
        tty->in_count--;
        if (dst[i] == '\n' && (tty->flags & TTY_FLAG_ICANON)) {
            return (int64_t)(i + 1);
        }
    }

    return (int64_t)to_read;
}

int64_t tty_write(tty_t *tty, const void *buf, size_t count) {
    if (!tty || !buf || count == 0) return 0;

    const char *src = (const char *)buf;
    for (size_t i = 0; i < count; i++) {
        /* vga_putchar already mirrors output to serial (COM1) */
        vga_putchar(src[i]);
    }

    return (int64_t)count;
}
