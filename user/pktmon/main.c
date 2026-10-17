/* =============================================================================
 * Nyota OS — Network Diagnostics & Packet Monitor (pktmon)
 * Captures network packets on eth0/lo, socket inspection, and traffic metrics.
 * =========================================================================== */

#include "libnyota.h"

static void format_ip(uint32_t ip, char *buf) {
    uint8_t *b = (uint8_t *)&ip;
    snprintf(buf, 16, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
}

static const char *proto_name(uint8_t proto) {
    if (proto == 6) return "TCP";
    if (proto == 17) return "UDP";
    if (proto == 1) return "ICMP";
    return "IP";
}

int main(int argc, char *argv[]) {
    bool show_stats = false;
    int capture_count = 5;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--stats") == 0) {
            show_stats = true;
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            capture_count = atoi(argv[++i]);
            if (capture_count <= 0) capture_count = 5;
        }
    }

    if (show_stats) {
        net_if_stat_t if_stats[8];
        socket_diag_t socks[32];
        memset(if_stats, 0, sizeof(if_stats));
        memset(socks, 0, sizeof(socks));

        if (pkt_mon(PKTMON_OP_STATS, if_stats, socks) < 0) {
            printf("pktmon: permission denied (root required for network diagnostics)\n");
            return 1;
        }

        printf("========================================\n");
        printf("       NYOTA NETWORK DIAGNOSTICS        \n");
        printf("========================================\n");
        printf("[Interface Statistics]\n");
        for (int i = 0; i < 8; i++) {
            if (if_stats[i].ifname[0] == '\0') continue;
            printf("  %s (MTU %u, %s):\n",
                   if_stats[i].ifname,
                   if_stats[i].mtu,
                   if_stats[i].is_up ? "UP" : "DOWN");
            printf("    RX: %lu packets, %lu bytes\n", if_stats[i].rx_packets, if_stats[i].rx_bytes);
            printf("    TX: %lu packets, %lu bytes\n", if_stats[i].tx_packets, if_stats[i].tx_bytes);
        }

        printf("\n[Active Sockets]\n");
        printf("  SOCK  PROTO  STATE         LOCAL ADDR        REMOTE ADDR\n");
        printf("  --------------------------------------------------------\n");
        int sock_found = 0;
        for (int i = 0; i < 32; i++) {
            if (socks[i].sock_id == 0 && socks[i].state == 0) continue;
            char l_ip[16], r_ip[16];
            format_ip(socks[i].local_ip, l_ip);
            format_ip(socks[i].remote_ip, r_ip);

            const char *st_str = "CLOSED";
            if (socks[i].state == 1) st_str = "LISTEN";
            else if (socks[i].state == 4) st_str = "ESTABLISHED";
            else if (socks[i].state == 2) st_str = "SYN_SENT";

            const char *pr_str = (socks[i].type == 1) ? "TCP" : ((socks[i].type == 2) ? "UDP" : "RAW");

            printf("  %-4u  %-5s  %-12s  %s:%u    %s:%u\n",
                   socks[i].sock_id, pr_str, st_str,
                   l_ip, socks[i].local_port,
                   r_ip, socks[i].remote_port);
            sock_found++;
        }
        if (sock_found == 0) {
            printf("  (no active sockets)\n");
        }
        printf("========================================\n");
        return 0;
    }

    /* Packet Capture Mode */
    if (pkt_mon(PKTMON_OP_START, NULL, NULL) < 0) {
        printf("pktmon: permission denied (root required for packet monitoring)\n");
        return 1;
    }

    printf("pktmon: capturing up to %d packets (headers only)...\n", capture_count);

    pkt_record_t records[32];
    int captured = 0;
    int timeout = 10; /* Up to 1 second */

    while (captured < capture_count && timeout-- > 0) {
        int n = (int)pkt_mon(PKTMON_OP_READ, records, (void *)32);
        for (int i = 0; i < n && captured < capture_count; i++) {
            char sip[16], dip[16];
            format_ip(records[i].src_ip, sip);
            format_ip(records[i].dst_ip, dip);

            printf("[%s %s] %s %s:%u -> %s:%u (len=%u)\n",
                   records[i].direction == 0 ? "RX" : "TX",
                   records[i].ifname,
                   proto_name(records[i].ip_proto),
                   sip, records[i].src_port,
                   dip, records[i].dst_port,
                   records[i].packet_len);
            captured++;
        }
        if (captured < capture_count) {
            sleep(100);
        }
    }

    pkt_mon(PKTMON_OP_STOP, NULL, NULL);

    printf("pktmon: capture complete (%d packets captured)\n", captured);
    return 0;
}
