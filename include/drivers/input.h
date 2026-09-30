/* =============================================================================
 * Nyota OS — Unified Input Subsystem Header
 * Normalized input events, bounded event queue, and wait queue synchronization.
 * =========================================================================== */

#ifndef NYOTA_DRIVERS_INPUT_H
#define NYOTA_DRIVERS_INPUT_H

#include "types.h"

/* Normalized Event Types */
typedef enum {
    EVENT_NONE                   = 0,
    EVENT_KEY_PRESS              = 1,
    EVENT_KEY_RELEASE            = 2,
    EVENT_MOUSE_MOVE             = 3,
    EVENT_MOUSE_BUTTON_PRESS     = 4,
    EVENT_MOUSE_BUTTON_RELEASE   = 5,
    EVENT_WINDOW_CLOSE           = 6,
    EVENT_WINDOW_FOCUS           = 7,
    EVENT_WINDOW_RESIZE          = 8
} input_event_type_t;

/* Normalized Input Event */
typedef struct {
    uint32_t type;            /* input_event_type_t */
    uint32_t keycode;         /* Key scancode / code */
    char     character;       /* ASCII character if printable */
    int32_t  mouse_x;         /* Screen X position */
    int32_t  mouse_y;         /* Screen Y position */
    int32_t  mouse_dx;        /* Movement delta X */
    int32_t  mouse_dy;        /* Movement delta Y */
    uint32_t mouse_buttons;   /* MOUSE_BTN_LEFT, etc. */
    uint32_t modifiers;       /* Shift, Ctrl, Alt */
    uint32_t window_id;       /* Target Window ID (if applicable) */
    uint64_t timestamp;       /* Monotonic timestamp in ms */
} __attribute__((packed)) input_event_t;

#define INPUT_EVENT_QUEUE_SIZE 128

/* Public Input APIs */
void input_init(void);
void input_post_event(const input_event_t *ev);
bool input_get_event(input_event_t *out_ev, bool blocking);

#endif /* NYOTA_DRIVERS_INPUT_H */
