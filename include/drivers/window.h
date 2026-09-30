/* =============================================================================
 * Nyota OS — Kernel Window Management Subsystem Interface
 * Defines window structures, Z-order, damage tracking, and server operations.
 * =========================================================================== */

#ifndef NYOTA_DRIVERS_WINDOW_H
#define NYOTA_DRIVERS_WINDOW_H

#include "types.h"
#include "drivers/input.h"

#define MAX_WINDOWS             32
#define WIN_TITLE_MAX           64
#define WIN_EVENT_QUEUE_LEN     32

/* Window States */
#define WIN_STATE_NORMAL        0
#define WIN_STATE_MINIMIZED     1
#define WIN_STATE_MAXIMIZED     2
#define WIN_STATE_CLOSED        3

/* Window Server Operations */
#define WS_OP_REGISTER          1
#define WS_OP_GET_WINDOWS       2
#define WS_OP_READ_PIXELS       3
#define WS_OP_SET_WINDOW_PROP   4
#define WS_OP_POST_EVENT        5
#define WS_OP_SET_DIRTY         6

typedef struct {
    uint32_t id;
    uint32_t owner_pid;
    int32_t  x;
    int32_t  y;
    uint32_t width;
    uint32_t height;
    char     title[WIN_TITLE_MAX];
    bool     visible;
    bool     focused;
    uint32_t z_order;
    uint32_t state;
    bool     dirty;
    uint32_t dirty_x, dirty_y, dirty_w, dirty_h;
} window_info_t;

typedef struct window {
    uint32_t id;
    uint32_t owner_pid;
    int32_t  x;
    int32_t  y;
    uint32_t width;
    uint32_t height;
    char     title[WIN_TITLE_MAX];
    bool     visible;
    bool     focused;
    uint32_t z_order;
    uint32_t state;
    bool     dirty;
    uint32_t dirty_x, dirty_y, dirty_w, dirty_h;
    uint32_t *buffer;          /* Backing pixel buffer (width * height * 4) */
    size_t   buffer_size;

    /* Bounded event queue */
    input_event_t event_queue[WIN_EVENT_QUEUE_LEN];
    uint32_t event_head;
    uint32_t event_tail;
    uint32_t event_count;

    struct process *waiter;    /* Waiting client process */
    bool in_use;
} window_t;

void window_subsystem_init(void);

int window_create(uint32_t owner_pid, uint32_t width, uint32_t height, const char *title);
int window_destroy(uint32_t win_id, uint32_t caller_pid);
int window_update(uint32_t win_id, uint32_t caller_pid, const void *user_pixels, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
int window_get_event(uint32_t win_id, uint32_t caller_pid, input_event_t *out_ev, bool block);
int window_post_event(uint32_t win_id, const input_event_t *ev);

void window_on_process_exit(uint32_t pid);

/* Server Operations */
int window_server_register(uint32_t pid);
bool window_server_is(uint32_t pid);
int window_server_get_windows(window_info_t *out_list, uint32_t max_count);
int window_server_read_pixels(uint32_t win_id, void *dest, size_t max_bytes);
int window_server_set_prop(uint32_t win_id, int32_t x, int32_t y, uint32_t state, bool focused, uint32_t z_order);

#endif /* NYOTA_DRIVERS_WINDOW_H */
