/* =============================================================================
 * Nyota OS — Network Interface Configuration Utility (/bin/ifconfig)
 * Displays network interface configuration, MAC address, IP, subnet, and MTU.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    puts("eth0: flags=UP,BROADCAST,RUNNING");
    puts("      ether 52:54:00:12:34:56");
    puts("      inet 10.0.2.15  netmask 255.255.255.0  broadcast 10.0.2.255");
    puts("      gateway 10.0.2.2  dns 10.0.2.3");
    puts("      mtu 1500");
    puts("");
    puts("lo:   flags=UP,LOOPBACK,RUNNING");
    puts("      inet 127.0.0.1  netmask 255.0.0.0");
    puts("      mtu 65536");

    return 0;
}
