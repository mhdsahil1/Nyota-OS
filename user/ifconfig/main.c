/* =============================================================================
 * Nyota OS — Network Interface Configuration Utility (/bin/ifconfig)
 * Displays and configures interface state, addresses, MTU, and packet statistics.
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

static void print_interface(const netdev_info_t *info) {
    printf("%s:\n", info->name);

    /* MAC address */
    char mac_str[32];
    format_mac(mac_str, sizeof(mac_str), info->mac);
    printf("  MAC:       %s\n", mac_str);

    /* IPv4 */
    struct in_addr in;
    in.s_addr = info->ip_addr;
    int prefix = netmask_to_prefix(info->netmask);
    printf("  IPv4:      %s/%d\n", inet_ntoa(in), prefix);

    /* Gateway */
    in.s_addr = info->gateway;
    printf("  Gateway:   %s\n", (info->gateway != 0) ? inet_ntoa(in) : "none");

    /* IPv6 */
    if (info->has_ip6) {
        char ip6_str[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, info->ip6_addr, ip6_str, sizeof(ip6_str));
        printf("  IPv6:      %s\n", ip6_str);
    } else {
        printf("  IPv6:      none\n");
    }

    /* MTU & State */
    printf("  MTU:       %u\n", info->mtu);
    printf("  State:     %s\n", (info->flags & 0x01) ? "UP" : "DOWN");

    /* Statistics */
    printf("  RX packets: %llu  bytes: %llu  errors: %llu  dropped: %llu\n",
           info->rx_packets, info->rx_bytes, info->rx_errors, info->rx_dropped);
    printf("  TX packets: %llu  bytes: %llu  errors: %llu  dropped: %llu\n",
           info->tx_packets, info->tx_bytes, info->tx_errors, info->tx_dropped);
}

int main(int argc, char **argv) {
    size_t count = (size_t)sys_net_ctrl(NET_CTRL_GET_COUNT, 0, 0);

    /* No arguments: display all interfaces */
    if (argc < 2) {
        if (count == 0) {
            puts("No network interfaces found.");
            return 0;
        }
        for (size_t i = 0; i < count; i++) {
            netdev_info_t info;
            memset(&info, 0, sizeof(info));
            if (sys_net_ctrl(NET_CTRL_GET_DEV, (uint64_t)i, (uint64_t)&info) == 0) {
                print_interface(&info);
                if (i + 1 < count) puts("");
            }
        }
        return 0;
    }

    /* Specific interface or configuration */
    const char *target_if = argv[1];

    /* Check if simply showing target interface */
    if (argc == 2) {
        for (size_t i = 0; i < count; i++) {
            netdev_info_t info;
            memset(&info, 0, sizeof(info));
            if (sys_net_ctrl(NET_CTRL_GET_DEV, (uint64_t)i, (uint64_t)&info) == 0) {
                if (strcmp(info.name, target_if) == 0) {
                    print_interface(&info);
                    return 0;
                }
            }
        }
        printf("ifconfig: interface '%s' not found\n", target_if);
        return 1;
    }

    /* Configure interface */
    netdev_conf_t conf;
    memset(&conf, 0, sizeof(conf));
    strncpy(conf.name, target_if, sizeof(conf.name) - 1);

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "up") == 0) {
            conf.set_up = true;
            conf.is_up = true;
        } else if (strcmp(argv[i], "down") == 0) {
            conf.set_up = true;
            conf.is_up = false;
        } else if (strcmp(argv[i], "netmask") == 0 && i + 1 < argc) {
            conf.netmask = inet_addr(argv[++i]);
        } else if (strcmp(argv[i], "mtu") == 0 && i + 1 < argc) {
            conf.mtu = (uint32_t)atoi(argv[++i]);
        } else {
            /* Positional IP address or Netmask */
            if (conf.ip_addr == 0) {
                conf.ip_addr = inet_addr(argv[i]);
                conf.set_up = true;
                conf.is_up = true;
            } else if (conf.netmask == 0) {
                conf.netmask = inet_addr(argv[i]);
            }
        }
    }
    if (conf.ip_addr != 0 && conf.netmask == 0) {
        conf.netmask = inet_addr("255.255.255.0");
    }

    int64_t ret = sys_net_ctrl(NET_CTRL_SET_DEV, (uint64_t)&conf, 0);
    if (ret != 0) {
        printf("ifconfig: failed to configure '%s' (err %lld)\n", target_if, ret);
        return 1;
    }

    printf("Configured interface '%s'\n", target_if);
    return 0;
}
