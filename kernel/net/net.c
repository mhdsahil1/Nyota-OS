/* =============================================================================
 * Nyota OS — Central Network Stack Implementation
 * =========================================================================== */

#include "net/net.h"
#include "net/ipv6.h"
#include "net/firewall.h"
#include "drivers/pci.h"
#include "drivers/e1000.h"
#include "vga.h"
#include "serial.h"

void net_init(void) {
    /* 1. Initialize Network Device Registry (creates lo) */
    netdev_init();

    /* 2. Initialize ARP table */
    arp_init();
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("[ OK ] ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_println("ARP");

    /* 3. Initialize IPv4 and Routing */
    ipv4_init();
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("[ OK ] ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_println("IPv4");

    /* 4. Initialize IPv6 */
    ipv6_init();
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("[ OK ] ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_println("IPv6");

    /* 5. Initialize ICMP */
    icmp_init();
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("[ OK ] ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_println("ICMP");

    /* 6. Initialize UDP */
    udp_init();
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("[ OK ] ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_println("UDP");

    /* 7. Initialize TCP */
    tcp_init();
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("[ OK ] ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_println("TCP");

    /* 8. Initialize Firewall */
    firewall_init();
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("[ OK ] ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_println("Firewall");

    /* 9. Initialize Socket Layer */
    socket_init();
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("[ OK ] ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_println("Sockets");

    /* 10. Initialize Intel E1000 Gigabit NIC */
    e1000_init();

    /* Re-evaluate routing table to ensure default route binds to newly added eth0 */
    route_init();

    net_device_t *eth = netdev_get_by_name("eth0");
    if (eth) {
        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        vga_print("[INFO]  ");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        vga_println("eth0");

        char buf[64];
        mac_format(eth->mac, buf, sizeof(buf));
        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        vga_print("[INFO]  ");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        vga_print("MAC: ");
        vga_println(buf);

        ip_format(eth->ip_addr.addr, buf, sizeof(buf));
        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        vga_print("[INFO]  ");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        vga_print("IP:  ");
        vga_println(buf);

        ip_format(eth->gateway.addr, buf, sizeof(buf));
        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        vga_print("[INFO]  ");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        vga_print("Gateway: ");
        vga_println(buf);

        if (eth->has_ip6) {
            ipv6_format((const ipv6_addr_t *)eth->ip6_addr, buf, sizeof(buf));
            vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
            vga_print("[INFO]  ");
            vga_set_color(VGA_WHITE, VGA_BLACK);
            vga_print("IPv6: ");
            vga_println(buf);
        }
    }
}

void net_timer_tick(void) {
    arp_timer_tick();
    tcp_timer_tick();
    e1000_poll_rx();
}

void net_poll(void) {
    e1000_poll_rx();
}
