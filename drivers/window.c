/* =============================================================================
 * Nyota OS — Window Management Subsystem Implementation
 * Window tracking, backing store allocation, event routing, and server hooks.
 * =========================================================================== */

#include "drivers/window.h"
#include "heap.h"
#include "memory.h"
#include "process.h"
#include "scheduler.h"
#include "syscall.h"
#include "serial.h"

static window_t g_windows[MAX_WINDOWS];
static uint32_t g_next_win_id = 1;
static uint32_t g_winserver_pid = 0;
static uint32_t g_max_z = 1;

void window_subsystem_init(void) {
    memset(g_windows, 0, sizeof(g_windows));
    g_next_win_id = 1;
    g_winserver_pid = 0;
    g_max_z = 1;
}

static window_t *find_window(uint32_t win_id) {
    if (win_id == 0) return NULL;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].in_use && g_windows[i].id == win_id) {
            return &g_windows[i];
        }
    }
    return NULL;
}

int window_create(uint32_t owner_pid, uint32_t width, uint32_t height, const char *title) {
    if (width == 0 || width > 1024 || height == 0 || height > 768) {
        return -SYS_ERR_EINVAL;
    }

    int slot = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!g_windows[i].in_use) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        return -SYS_ERR_ENOMEM;
    }

    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].in_use) g_windows[i].focused = false;
    }

    size_t buf_size = (size_t)width * height * sizeof(uint32_t);
    uint32_t *buf = (uint32_t *)kmalloc(buf_size);
    if (!buf) {
        return -SYS_ERR_ENOMEM;
    }

    /* Fill with default client window background (dark theme #1E1E2E) */
    for (size_t i = 0; i < (size_t)width * height; i++) {
        buf[i] = 0xFF1E1E2E;
    }

    window_t *w = &g_windows[slot];
    memset(w, 0, sizeof(window_t));

    w->id = g_next_win_id++;
    w->owner_pid = owner_pid;
    w->width = width;
    w->height = height;

    /* Cascade window positioning */
    uint32_t offset = (w->id % 10) * 32;
    w->x = 80 + (int32_t)offset;
    w->y = 60 + (int32_t)offset;
    if (w->x + (int32_t)w->width > 1024) w->x = 1024 - (int32_t)w->width;
    if (w->y + (int32_t)w->height > 768) w->y = 768 - (int32_t)w->height;
    if (w->x < 0) w->x = 0;
    if (w->y < 32) w->y = 32;

    if (title) {
        strncpy(w->title, title, WIN_TITLE_MAX - 1);
        w->title[WIN_TITLE_MAX - 1] = '\0';
    } else {
        strcpy(w->title, "Nyota Window");
    }

    w->visible = true;
    w->focused = true;
    w->z_order = ++g_max_z;
    w->state = WIN_STATE_NORMAL;
    w->dirty = true;
    w->dirty_x = 0;
    w->dirty_y = 0;
    w->dirty_w = width;
    w->dirty_h = height;
    w->buffer = buf;
    w->buffer_size = buf_size;
    w->in_use = true;

    return (int)w->id;
}

int window_destroy(uint32_t win_id, uint32_t caller_pid) {
    window_t *w = find_window(win_id);
    if (!w) return -SYS_ERR_EBADF;

    if (caller_pid != 0 && caller_pid != w->owner_pid && caller_pid != g_winserver_pid) {
        return -SYS_ERR_EACCES;
    }

    if (w->waiter) {
        scheduler_wake(w->waiter);
        w->waiter = NULL;
    }

    if (w->buffer) {
        kfree(w->buffer);
        w->buffer = NULL;
    }

    w->in_use = false;
    w->state = WIN_STATE_CLOSED;
    w->dirty = true;

    return 0;
}

int window_update(uint32_t win_id, uint32_t caller_pid, const void *user_pixels, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    window_t *win = find_window(win_id);
    if (!win) return -SYS_ERR_EBADF;

    if (caller_pid != 0 && caller_pid != win->owner_pid && caller_pid != g_winserver_pid) {
        return -SYS_ERR_EACCES;
    }

    if (x >= win->width || y >= win->height || w == 0 || h == 0) {
        return 0;
    }

    if (x + w > win->width)  w = win->width - x;
    if (y + h > win->height) h = win->height - y;

    /* Copy row by row from user buffer to window backing store */
    const uint32_t *src = (const uint32_t *)user_pixels;
    for (uint32_t r = 0; r < h; r++) {
        uint32_t dst_idx = (y + r) * win->width + x;
        uint32_t src_idx = r * w;
        memcpy(&win->buffer[dst_idx], &src[src_idx], w * sizeof(uint32_t));
    }

    win->dirty = true;
    win->dirty_x = x;
    win->dirty_y = y;
    win->dirty_w = w;
    win->dirty_h = h;

    return 0;
}

int window_get_event(uint32_t win_id, uint32_t caller_pid, input_event_t *out_ev, bool block) {
    window_t *w = find_window(win_id);
    if (!w) return -SYS_ERR_EBADF;

    if (caller_pid != 0 && caller_pid != w->owner_pid && caller_pid != g_winserver_pid) {
        return -SYS_ERR_EACCES;
    }

    while (w->in_use && w->event_count == 0) {
        if (!block) return 0;

        process_t *curr = process_get_current();
        if (!curr || curr == scheduler_get_idle()) return 0;

        w->waiter = curr;
        curr->state = PROCESS_SLEEPING;
        scheduler_remove(curr);
        scheduler_request_reschedule();
        __asm__ volatile ("hlt");

        /* Re-lookup window after wakeup to verify it wasn't destroyed */
        w = find_window(win_id);
        if (!w || !w->in_use) return -SYS_ERR_EBADF;
    }

    if (!w->in_use) return -SYS_ERR_EBADF;
    if (w->event_count == 0) return 0;

    *out_ev = w->event_queue[w->event_head];
    w->event_head = (w->event_head + 1) % WIN_EVENT_QUEUE_LEN;
    w->event_count--;

    return 1;
}

int window_post_event(uint32_t win_id, const input_event_t *ev) {
    window_t *w = find_window(win_id);
    if (!w || !w->in_use) return -SYS_ERR_EBADF;

    if (w->event_count >= WIN_EVENT_QUEUE_LEN) {
        /* Drop oldest event if queue is full */
        w->event_head = (w->event_head + 1) % WIN_EVENT_QUEUE_LEN;
        w->event_count--;
    }

    w->event_queue[w->event_tail] = *ev;
    w->event_tail = (w->event_tail + 1) % WIN_EVENT_QUEUE_LEN;
    w->event_count++;

    if (w->waiter) {
        scheduler_wake(w->waiter);
        w->waiter = NULL;
    }

    return 0;
}

void window_on_process_exit(uint32_t pid) {
    if (pid == g_winserver_pid) {
        g_winserver_pid = 0;
    }

    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_windows[i].in_use && g_windows[i].owner_pid == pid) {
            window_destroy(g_windows[i].id, pid);
        }
    }
}

/* ── Server Operations ────────────────────────────────────────────────────── */

int window_server_register(uint32_t pid) {
    if (pid == 0 || (g_winserver_pid != 0 && g_winserver_pid != pid)) {
        return -SYS_ERR_EACCES;
    }
    g_winserver_pid = pid;
    return 0;
}

bool window_server_is(uint32_t pid) {
    return pid != 0 && pid == g_winserver_pid;
}

int window_server_get_windows(window_info_t *out_list, uint32_t max_count) {
    if (!out_list || max_count == 0) return 0;

    uint32_t count = 0;
    for (int i = 0; i < MAX_WINDOWS && count < max_count; i++) {
        if (g_windows[i].in_use) {
            window_t *w = &g_windows[i];
            out_list[count].id = w->id;
            out_list[count].owner_pid = w->owner_pid;
            out_list[count].x = w->x;
            out_list[count].y = w->y;
            out_list[count].width = w->width;
            out_list[count].height = w->height;
            strncpy(out_list[count].title, w->title, WIN_TITLE_MAX - 1);
            out_list[count].title[WIN_TITLE_MAX - 1] = '\0';
            out_list[count].visible = w->visible;
            out_list[count].focused = w->focused;
            out_list[count].z_order = w->z_order;
            out_list[count].state = w->state;
            out_list[count].dirty = w->dirty;
            out_list[count].dirty_x = w->dirty_x;
            out_list[count].dirty_y = w->dirty_y;
            out_list[count].dirty_w = w->dirty_w;
            out_list[count].dirty_h = w->dirty_h;
            count++;
        }
    }

    return (int)count;
}

int window_server_read_pixels(uint32_t win_id, void *dest, size_t max_bytes) {
    window_t *w = find_window(win_id);
    if (!w || !w->in_use || !w->buffer) return -SYS_ERR_EBADF;

    size_t copy_bytes = w->buffer_size;
    if (copy_bytes > max_bytes) copy_bytes = max_bytes;

    memcpy(dest, w->buffer, copy_bytes);
    w->dirty = false;

    return (int)copy_bytes;
}

int window_server_set_prop(uint32_t win_id, int32_t x, int32_t y, uint32_t state, bool focused, uint32_t z_order) {
    window_t *w = find_window(win_id);
    if (!w || !w->in_use) return -SYS_ERR_EBADF;

    if (focused) {
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (g_windows[i].in_use) g_windows[i].focused = false;
        }
    }
    w->x = x;
    w->y = y;
    w->state = state;
    w->focused = focused;
    w->z_order = z_order;
    w->dirty = true;

    return 0;
}
