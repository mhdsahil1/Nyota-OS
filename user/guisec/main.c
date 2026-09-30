#include "libnyota.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    int denied = 0;
    input_event_t event;
    window_info_t windows[1];
    uint32_t pixel = 0;

    if (input_poll_event(&event, 0) == -13) denied++;
    if (gfx_flip(&pixel, sizeof(pixel)) == -13) denied++;
    if (win_server_op(WS_OP_REGISTER, NULL, NULL, NULL) == -13) denied++;
    if (win_server_op(WS_OP_GET_WINDOWS, windows, (void *)1, NULL) == -13) denied++;

    if (denied == 4) {
        printf("GUI security: non-server global input, framebuffer, and window-server access denied\n");
        return 0;
    }

    printf("GUI security failure: %d of 4 privileged operations denied\n", denied);
    return 1;
}
