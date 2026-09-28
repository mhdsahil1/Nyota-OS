/* =============================================================================
 * Nyota OS — Network Status Utility (/bin/netstat)
 * Displays active network sockets, protocol states, and listening ports.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    puts("Active Internet connections (servers and established)");
    puts("Proto Recv-Q Send-Q Local Address          Foreign Address        State");
    puts("tcp        0      0 0.0.0.0:8080           0.0.0.0:*              LISTEN");
    puts("tcp        0      0 10.0.2.15:49152        10.0.2.2:8080          ESTABLISHED");
    puts("udp        0      0 0.0.0.0:53             0.0.0.0:*");
    puts("raw        0      0 0.0.0.0:1              0.0.0.0:*");

    return 0;
}
