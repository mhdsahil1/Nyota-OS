/* =============================================================================
 * Nyota OS — PS/2 Mouse Driver Header
 * 3-button mouse state, screen bounds clamping, and packet processing.
 * =========================================================================== */

#ifndef NYOTA_DRIVERS_MOUSE_H
#define NYOTA_DRIVERS_MOUSE_H

#include "types.h"

#define MOUSE_BTN_LEFT   (1 << 0)
#define MOUSE_BTN_RIGHT  (1 << 1)
#define MOUSE_BTN_MIDDLE (1 << 2)

typedef struct {
    int32_t  x;
    int32_t  y;
    int32_t  dx;
    int32_t  dy;
    uint32_t buttons;
    int32_t  max_x;
    int32_t  max_y;
} mouse_state_t;

/* Public Mouse APIs */
void mouse_init(void);
mouse_state_t *mouse_get_state(void);
void mouse_set_bounds(int32_t max_x, int32_t max_y);

#endif /* NYOTA_DRIVERS_MOUSE_H */
