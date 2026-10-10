/* =============================================================================
 * Nyota OS — IP Route & Link Management Utility (/bin/ip)
 * Displays routing tables, manages static routes, and inspects network links.
 * =========================================================================== */

#include "libnyota.h"

static int netmask_to_prefix(uint32_t mask) {
    int count = 0;
    uint32_t m = ntohl(mask);
    while (m & 0x80000000) {
        count++;
        m <<= 1;
    }
    return count;
}

static uint32_t prefix_to_netmask(int prefix) {
    if (prefix <= 0) return 0;
    if (prefix >= 32) return 0xFFFFFFFF;
    uint32_t m = 0;
    for (int i = 0; i < prefix; i++) {
        m |= (1U << (31 - i));
    }
    return htonl(m);
}

static void show_routes(void) {
    route_info_t routes[32];
    memset(routes, 0, sizeof(routes));
    int64_t count = sys_net_ctrl(NET_CTRL_GET_ROUTES, (uint64_t)routes, 32);
    if (count <= 0) {
        puts("No routes in table.");
        return;
    }

    printf("%-16s %-13s %-10s\n", "Destination", "Gateway", "Interface");

    for (int64_t i = 0; i < count; i++) {
        char dest_str[32];
        struct in_addr in;
        in.s_addr = routes[i].dest;
        int pfx = netmask_to_prefix(routes[i].netmask);
        if (routes[i].dest == 0 && routes[i].netmask == 0) {
            strcpy(dest_str, "0.0.0.0/0");
        } else {
            snprintf(dest_str, sizeof(dest_str), "%s/%d", inet_ntoa(in), pfx);
        }

        char gw_str[32];
        if (routes[i].gateway == 0) {
            strcpy(gw_str, "direct");
        } else {
            in.s_addr = routes[i].gateway;
            strncpy(gw_str, inet_ntoa(in), sizeof(gw_str) - 1);
        }

        printf("%-16s %-13s %-10s\n", dest_str, gw_str, routes[i].ifname);
    }
}

static void format_mac(char *buf, size_t max, const uint8_t *mac) {
    const char *hex = "0123456789abcdef";
    snprintf(buf, max, "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
             hex[(mac[0] >> 4) & 0xF], hex[mac[0] & 0xF],
             hex[(mac[1] >> 4) & 0xF], hex[mac[1] & 0xF],
             hex[(mac[2] >> 4) & 0xF], hex[mac[2] & 0xF],
             hex[(mac[3] >> 4) & 0xF], hex[mac[3] & 0xF],
             hex[(mac[4] >> 4) & 0xF], hex[mac[4] & 0xF],
             hex[(mac[5] >> 4) & 0xF], hex[mac[5] & 0xF]);
}

static void show_links(void) {
    size_t count = (size_t)sys_net_ctrl(NET_CTRL_GET_COUNT, 0, 0);
    for (size_t i = 0; i < count; i++) {
        netdev_info_t info;
        memset(&info, 0, sizeof(info));
        if (sys_net_ctrl(NET_CTRL_GET_DEV, (uint64_t)i, (uint64_t)&info) == 0) {
            printf("%u: %s: <", (uint32_t)(i + 1), info.name);
            if (info.flags & 0x01) printf("UP");
            else printf("DOWN");
            if (info.flags & 0x02) printf(",BROADCAST");
            if (info.flags & 0x04) printf(",LOOPBACK");
            if (info.flags & 0x08) printf(",RUNNING");
            if (info.flags & 0x10) printf(",MULTICAST");
            printf("> mtu %u state %s\n", info.mtu, info.link_up ? "UP" : "DOWN");
            char mac_str[32];
            format_mac(mac_str, sizeof(mac_str), info.mac);
            printf("    link/ether %s\n", mac_str);
        }
    }
}

static void show_addrs(void) {
    size_t count = (size_t)sys_net_ctrl(NET_CTRL_GET_COUNT, 0, 0);
    for (size_t i = 0; i < count; i++) {
        netdev_info_t info;
        memset(&info, 0, sizeof(info));
        if (sys_net_ctrl(NET_CTRL_GET_DEV, (uint64_t)i, (uint64_t)&info) == 0) {
            printf("%u: %s: mtu %u\n", (uint32_t)(i + 1), info.name, info.mtu);
            struct in_addr in;
            in.s_addr = info.ip_addr;
            int pfx = netmask_to_prefix(info.netmask);
            printf("    inet %s/%d scope global %s\n", inet_ntoa(in), pfx, info.name);
            if (info.has_ip6) {
                char ip6_str[INET6_ADDRSTRLEN];
                inet_ntop(AF_INET6, info.ip6_addr, ip6_str, sizeof(ip6_str));
                printf("    inet6 %s scope link\n", ip6_str);
            }
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        show_routes();
        return 0;
    }

    const char *sub = argv[1];

    if (strcmp(sub, "route") == 0 || strcmp(sub, "r") == 0) {
        if (argc == 2 || strcmp(argv[2], "show") == 0 || strcmp(argv[2], "list") == 0) {
            show_routes();
            return 0;
        }

        if (strcmp(argv[2], "add") == 0) {
            if (argc < 4) {
                puts("Usage: ip route add <dest[/prefix]> [via <gw>] [dev <if>]");
                return 1;
            }

            char dest_buf[64];
            strncpy(dest_buf, argv[3], sizeof(dest_buf) - 1);
            char *slash = strchr(dest_buf, '/');
            int pfx = 32;
            if (slash) {
                *slash = '\0';
                pfx = atoi(slash + 1);
            }

            uint32_t dest = inet_addr(dest_buf);
            uint32_t mask = prefix_to_netmask(pfx);
            uint32_t gw = 0;
            char ifname[16] = "";

            for (int i = 4; i < argc; i++) {
                if (strcmp(argv[i], "via") == 0 && i + 1 < argc) {
                    gw = inet_addr(argv[++i]);
                } else if (strcmp(argv[i], "dev") == 0 && i + 1 < argc) {
                    strncpy(ifname, argv[++i], sizeof(ifname) - 1);
                }
            }

            route_info_t r;
            memset(&r, 0, sizeof(r));
            r.dest = dest;
            r.netmask = mask;
            r.gateway = gw;
            strncpy(r.ifname, ifname, sizeof(r.ifname) - 1);

            int64_t ret = sys_net_ctrl(NET_CTRL_ADD_ROUTE, (uint64_t)&r, 0);
            if (ret != 0) {
                printf("ip: failed to add route (err %lld)\n", ret);
                return 1;
            }
            puts("Route added.");
            return 0;
        }

        if (strcmp(argv[2], "del") == 0 || strcmp(argv[2], "delete") == 0) {
            if (argc < 4) {
                puts("Usage: ip route del <dest[/prefix]>");
                return 1;
            }

            char dest_buf[64];
            strncpy(dest_buf, argv[3], sizeof(dest_buf) - 1);
            char *slash = strchr(dest_buf, '/');
            int pfx = 32;
            if (slash) {
                *slash = '\0';
                pfx = atoi(slash + 1);
            }

            uint32_t dest = inet_addr(dest_buf);
            uint32_t mask = prefix_to_netmask(pfx);

            int64_t ret = sys_net_ctrl(NET_CTRL_DEL_ROUTE, (uint64_t)dest, (uint64_t)mask);
            if (ret != 0) {
                printf("ip: failed to delete route (err %lld)\n", ret);
                return 1;
            }
            puts("Route deleted.");
            return 0;
        }

        puts("Unknown ip route command.");
        return 1;
    }

    if (strcmp(sub, "link") == 0 || strcmp(sub, "l") == 0) {
        show_links();
        return 0;
    }

    if (strcmp(sub, "addr") == 0 || strcmp(sub, "a") == 0) {
        show_addrs();
        return 0;
    }

    printf("Usage: ip [route|link|addr] ...\n");
    return 1;
}
