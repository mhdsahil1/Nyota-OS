/* =============================================================================
 * Nyota OS — Anonymous Pipe IPC Implementation (Phase 8)
 * Bounded circular buffer, scheduler sleeping, and POSIX EOF/SIGPIPE semantics.
 * =========================================================================== */

#include "ipc/pipe.h"
#include "heap.h"
#include "memory.h"
#include "process.h"
#include "scheduler.h"
#include "signal.h"
#include "syscall.h"

#define MAX_SYSTEM_PIPES    32

static pipe_t system_pipes[MAX_SYSTEM_PIPES];
static uint32_t active_pipe_count = 0;

void pipe_init(void) {
    memset(system_pipes, 0, sizeof(system_pipes));
    active_pipe_count = 0;
}

uint32_t pipe_active_count(void) {
    return active_pipe_count;
}

pipe_t *pipe_create(void) {
    for (size_t i = 0; i < MAX_SYSTEM_PIPES; i++) {
        if (!system_pipes[i].in_use) {
            memset(&system_pipes[i], 0, sizeof(pipe_t));
            system_pipes[i].in_use = true;
            system_pipes[i].readers_count = 1;
            system_pipes[i].writers_count = 1;
            system_pipes[i].ref_count = 2;
            active_pipe_count++;
            return &system_pipes[i];
        }
    }
    return NULL;
}

static void pipe_free(pipe_t *pipe) {
    if (!pipe) return;
    if (pipe->in_use) {
        pipe->in_use = false;
        if (active_pipe_count > 0) active_pipe_count--;
    }
}

int64_t pipe_read(pipe_t *pipe, void *buf, size_t count) {
    if (!pipe || !buf) return -SYS_ERR_EFAULT;
    if (count == 0) return 0;

    process_t *curr = process_get_current();

    /* Wait for data or EOF */
    while (pipe->count == 0) {
        if (pipe->writers_count == 0) {
            return 0; /* All writers closed -> EOF */
        }
        if (curr) {
            pipe->reader_waiter = curr;
            curr->state = PROCESS_SLEEPING;
            scheduler_remove(curr);
            scheduler_request_reschedule();
            pipe->reader_waiter = NULL;
        } else {
            break;
        }
    }

    size_t to_read = count;
    if (to_read > pipe->count) {
        to_read = pipe->count;
    }

    uint8_t *dst = (uint8_t *)buf;
    for (size_t i = 0; i < to_read; i++) {
        dst[i] = pipe->buffer[pipe->read_pos];
        pipe->read_pos = (pipe->read_pos + 1) % PIPE_CAPACITY;
    }
    pipe->count -= to_read;

    /* Wake waiting writer if any */
    if (pipe->writer_waiter) {
        process_t *w = pipe->writer_waiter;
        pipe->writer_waiter = NULL;
        scheduler_wake(w);
    }

    return (int64_t)to_read;
}

int64_t pipe_write(pipe_t *pipe, const void *buf, size_t count) {
    if (!pipe || !buf) return -SYS_ERR_EFAULT;
    if (count == 0) return 0;

    process_t *curr = process_get_current();

    if (pipe->readers_count == 0) {
        if (curr) {
            signal_send(curr, SIGPIPE);
        }
        return -SYS_ERR_EPIPE;
    }

    const uint8_t *src = (const uint8_t *)buf;
    size_t written = 0;

    while (written < count) {
        if (pipe->readers_count == 0) {
            if (curr) {
                signal_send(curr, SIGPIPE);
            }
            return written > 0 ? (int64_t)written : -SYS_ERR_EPIPE;
        }

        while (pipe->count == PIPE_CAPACITY) {
            if (pipe->readers_count == 0) {
                if (curr) signal_send(curr, SIGPIPE);
                return written > 0 ? (int64_t)written : -SYS_ERR_EPIPE;
            }
            if (curr) {
                pipe->writer_waiter = curr;
                curr->state = PROCESS_SLEEPING;
                scheduler_remove(curr);
                scheduler_request_reschedule();
                pipe->writer_waiter = NULL;
            } else {
                break;
            }
        }

        size_t space = PIPE_CAPACITY - pipe->count;
        size_t chunk = count - written;
        if (chunk > space) chunk = space;

        for (size_t i = 0; i < chunk; i++) {
            pipe->buffer[pipe->write_pos] = src[written++];
            pipe->write_pos = (pipe->write_pos + 1) % PIPE_CAPACITY;
        }
        pipe->count += chunk;

        /* Wake waiting reader */
        if (pipe->reader_waiter) {
            process_t *r = pipe->reader_waiter;
            pipe->reader_waiter = NULL;
            scheduler_wake(r);
        }
    }

    return (int64_t)written;
}

void pipe_close_read(pipe_t *pipe) {
    if (!pipe) return;
    if (pipe->readers_count > 0) {
        pipe->readers_count--;
    }
    /* Wake writer so it can receive SIGPIPE/EPIPE */
    if (pipe->writer_waiter) {
        process_t *w = pipe->writer_waiter;
        pipe->writer_waiter = NULL;
        scheduler_wake(w);
    }
    if (pipe->ref_count > 0) {
        pipe->ref_count--;
        if (pipe->ref_count == 0) {
            pipe_free(pipe);
        }
    }
}

void pipe_close_write(pipe_t *pipe) {
    if (!pipe) return;
    if (pipe->writers_count > 0) {
        pipe->writers_count--;
    }
    /* Wake reader so it receives EOF */
    if (pipe->reader_waiter) {
        process_t *r = pipe->reader_waiter;
        pipe->reader_waiter = NULL;
        scheduler_wake(r);
    }
    if (pipe->ref_count > 0) {
        pipe->ref_count--;
        if (pipe->ref_count == 0) {
            pipe_free(pipe);
        }
    }
}

void pipe_close_file(file_t *f) {
    if (!f || f->type != FILE_TYPE_PIPE || !f->filesystem_data) return;
    pipe_t *p = (pipe_t *)f->filesystem_data;
    if (f->flags == O_WRONLY) {
        pipe_close_write(p);
    } else {
        pipe_close_read(p);
    }
}

int pipe_alloc_pair(int fds[2]) {
    if (!fds) return -SYS_ERR_EFAULT;

    process_t *curr = process_get_current();
    if (!curr) return -SYS_ERR_EBADF;

    pipe_t *p = pipe_create();
    if (!p) return -SYS_ERR_ENOMEM;

    /* Allocate read file object from VFS pool */
    file_t *rf = vfs_alloc_file();
    if (!rf) {
        pipe_free(p);
        return -SYS_ERR_ENOMEM;
    }
    rf->type = FILE_TYPE_PIPE;
    rf->flags = O_RDONLY;
    rf->filesystem_data = p;
    rf->ref_count = 1;

    /* Allocate write file object from VFS pool */
    file_t *wf = vfs_alloc_file();
    if (!wf) {
        rf->ref_count = 0;
        pipe_free(p);
        return -SYS_ERR_ENOMEM;
    }
    wf->type = FILE_TYPE_PIPE;
    wf->flags = O_WRONLY;
    wf->filesystem_data = p;
    wf->ref_count = 1;

    int rfd = vfs_alloc_fd(curr->fds, rf);
    if (rfd < 0) {
        rf->ref_count = 0;
        wf->ref_count = 0;
        pipe_free(p);
        return -SYS_ERR_ENOSPC;
    }

    int wfd = vfs_alloc_fd(curr->fds, wf);
    if (wfd < 0) {
        curr->fds[rfd] = NULL;
        rf->ref_count = 0;
        wf->ref_count = 0;
        pipe_free(p);
        return -SYS_ERR_ENOSPC;
    }

    fds[0] = rfd;
    fds[1] = wfd;
    return 0;
}
