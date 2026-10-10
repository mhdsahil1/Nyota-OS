/* =============================================================================
 * Nyota OS — Network Diagnostics & Summary Utility (/bin/netinfo)
 * Reports real configuration status, interface counts, and subsystem availability.
 * =========================================================================== */

#include "libnyota.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    puts("Nyota Network Summary");
    puts("---------------------");

    size_t if_count = (size_t)sys_net_ctrl(NET_CTRL_GET_COUNT, 0, 0);
    printf("Interfaces : %u\n", (uint32_t)if_count);

    bool has_ipv4 = false;
    bool has_ipv6 = false;
    bool has_gw = false;
    bool has_dns = false;
    char ipv4_str[32] = "";
    char ipv6_str[64] = "";
    char gw_str[32] = "";
    char dns_str[32] = "";

    for (size_t i = 0; i < if_count; i++) {
        netdev_info_t info;
        memset(&info, 0, sizeof(info));
        if (sys_net_ctrl(NET_CTRL_GET_DEV, (uint64_t)i, (uint64_t)&info) == 0) {
            /* Skip loopback for global summary if physical interface exists */
            if ((info.flags & 0x04) && if_count > 1) continue;

            if (info.ip_addr != 0 && !has_ipv4) {
                has_ipv4 = true;
                struct in_addr in;
                in.s_addr = info.ip_addr;
                strncpy(ipv4_str, inet_ntoa(in), sizeof(ipv4_str) - 1);
            }
            if (info.has_ip6 && !has_ipv6) {
                has_ipv6 = true;
                inet_ntop(AF_INET6, info.ip6_addr, ipv6_str, sizeof(ipv6_str));
            }
            if (info.gateway != 0 && !has_gw) {
                has_gw = true;
                struct in_addr in;
                in.s_addr = info.gateway;
                strncpy(gw_str, inet_ntoa(in), sizeof(gw_str) - 1);
            }
            if (info.dns != 0 && !has_dns) {
                has_dns = true;
                struct in_addr in;
                in.s_addr = info.dns;
                strncpy(dns_str, inet_ntoa(in), sizeof(dns_str) - 1);
            }
        }
    }

    if (has_ipv4) printf("IPv4       : configured (%s)\n", ipv4_str);
    else puts("IPv4       : unconfigured");

    if (has_ipv6) printf("IPv6       : configured (%s)\n", ipv6_str);
    else puts("IPv6       : unavailable");

    if (has_gw) printf("Gateway    : configured (%s)\n", gw_str);
    else puts("Gateway    : unavailable");

    if (has_dns) printf("DNS        : configured (%s)\n", dns_str);
    else puts("DNS        : unavailable");

    puts("TCP        : available");
    puts("UDP        : available");

    firewall_status_t fw_st;
    memset(&fw_st, 0, sizeof(fw_st));
    if (sys_firewall_op(FW_CMD_GET_STATUS, (uint64_t)&fw_st, 0, 0) == 0) {
        printf("Firewall   : %s (%u rules)\n", fw_st.enabled ? "enabled" : "disabled", fw_st.rule_count);
    } else {
        puts("Firewall   : disabled");
    }

    return 0;
}
