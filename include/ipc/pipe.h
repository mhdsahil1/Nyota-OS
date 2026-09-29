/* =============================================================================
 * Nyota OS — Anonymous Pipe IPC Interface (Phase 8)
 * Circular buffer abstraction, non-spinning wait queues, and EOF semantics.
 * =========================================================================== */

#ifndef NYOTA_IPC_PIPE_H
#define NYOTA_IPC_PIPE_H

#include "types.h"
#include "fs/vfs.h"

#define PIPE_CAPACITY   4096

struct process;

typedef struct pipe {
    uint8_t buffer[PIPE_CAPACITY];
    size_t  read_pos;
    size_t  write_pos;
    size_t  count;

    uint32_t readers_count;
    uint32_t writers_count;

    struct process *reader_waiter;
    struct process *writer_waiter;

    uint32_t ref_count;
    bool in_use;
} pipe_t;

void pipe_init(void);
pipe_t *pipe_create(void);
int pipe_alloc_pair(int fds[2]);
int64_t pipe_read(pipe_t *pipe, void *buf, size_t count);
int64_t pipe_write(pipe_t *pipe, const void *buf, size_t count);
void pipe_close_read(pipe_t *pipe);
void pipe_close_write(pipe_t *pipe);
void pipe_close_file(file_t *f);
uint32_t pipe_active_count(void);

#endif /* NYOTA_IPC_PIPE_H */
