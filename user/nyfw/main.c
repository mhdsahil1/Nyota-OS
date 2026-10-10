/* =============================================================================
 * Nyota OS — Network Packet-Filtering Firewall Utility (/bin/nyfw)
 * Controls firewall ruleset, policies, packet inspection filters, and counters.
 * =========================================================================== */

#include "libnyota.h"

static void show_status(void) {
    firewall_status_t st;
    memset(&st, 0, sizeof(st));
    if (sys_firewall_op(FW_CMD_GET_STATUS, (uint64_t)&st, 0, 0) != 0) {
        puts("nyfw: failed to get firewall status");
        return;
    }

    printf("Firewall: %s\n", st.enabled ? "ENABLED" : "DISABLED");
    printf("Default inbound policy: %s\n", (st.default_in_policy == FIREWALL_ACTION_ACCEPT) ? "ACCEPT" : "DROP");
    printf("Default outbound policy: %s\n", (st.default_out_policy == FIREWALL_ACTION_ACCEPT) ? "ACCEPT" : "DROP");
    printf("Rules: %u\n", st.rule_count);
    printf("Total inbound packets : %llu\n", st.total_in_packets);
    printf("Total outbound packets: %llu\n", st.total_out_packets);
    printf("Accepted packets      : %llu\n", st.total_accepted);
    printf("Dropped packets       : %llu\n", st.total_dropped);
}

static const char *proto_name(uint8_t proto) {
    switch (proto) {
        case FIREWALL_PROTO_ICMP:   return "icmp";
        case FIREWALL_PROTO_TCP:    return "tcp";
        case FIREWALL_PROTO_UDP:    return "udp";
        case FIREWALL_PROTO_ICMPV6: return "icmp6";
        default:                    return "any";
    }
}

static const char *dir_name(uint8_t dir) {
    switch (dir) {
        case FIREWALL_DIR_IN:   return "IN";
        case FIREWALL_DIR_OUT:  return "OUT";
        case FIREWALL_DIR_BOTH: return "BOTH";
        default:                return "?";
    }
}

static void show_rules(void) {
    firewall_rule_t rules[FIREWALL_MAX_RULES];
    memset(rules, 0, sizeof(rules));
    int64_t count = sys_firewall_op(FW_CMD_GET_RULES, (uint64_t)rules, FIREWALL_MAX_RULES, 0);
    if (count <= 0) {
        puts("No firewall rules configured.");
        return;
    }

    printf("%-3s %-4s %-6s %-5s %-16s %-16s %-5s %-5s %-6s %-8s\n",
           "ID", "DIR", "ACT", "PROT", "SRC", "DST", "SPORT", "DPORT", "IF", "MATCH");

    for (int64_t i = 0; i < count; i++) {
        char src_str[24] = "any";
        char dst_str[24] = "any";
        struct in_addr in;
        if (rules[i].src_ip != 0) {
            in.s_addr = rules[i].src_ip;
            strncpy(src_str, inet_ntoa(in), sizeof(src_str) - 1);
        }
        if (rules[i].dst_ip != 0) {
            in.s_addr = rules[i].dst_ip;
            strncpy(dst_str, inet_ntoa(in), sizeof(dst_str) - 1);
        }

        char sp_str[8] = "any";
        char dp_str[8] = "any";
        if (rules[i].src_port_start != 0) snprintf(sp_str, sizeof(sp_str), "%u", rules[i].src_port_start);
        if (rules[i].dst_port_start != 0) snprintf(dp_str, sizeof(dp_str), "%u", rules[i].dst_port_start);

        printf("%-3u %-4s %-6s %-5s %-16s %-16s %-5s %-5s %-6s %-8llu\n",
               rules[i].id,
               dir_name(rules[i].direction),
               (rules[i].action == FIREWALL_ACTION_ACCEPT) ? "ACCEPT" : "DROP",
               proto_name(rules[i].protocol),
               src_str, dst_str, sp_str, dp_str,
               rules[i].ifname[0] ? rules[i].ifname : "any",
               rules[i].packets_matched);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        show_status();
        return 0;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "status") == 0) {
        show_status();
        return 0;
    }

    if (strcmp(cmd, "list") == 0 || strcmp(cmd, "l") == 0) {
        show_rules();
        return 0;
    }

    if (strcmp(cmd, "enable") == 0) {
        int64_t ret = sys_firewall_op(FW_CMD_SET_ENABLED, 1, 0, 0);
        if (ret == 0) puts("Firewall enabled.");
        else printf("nyfw: failed to enable firewall (err %lld)\n", ret);
        return (int)ret;
    }

    if (strcmp(cmd, "disable") == 0) {
        int64_t ret = sys_firewall_op(FW_CMD_SET_ENABLED, 0, 0, 0);
        if (ret == 0) puts("Firewall disabled.");
        else printf("nyfw: failed to disable firewall (err %lld)\n", ret);
        return (int)ret;
    }

    if (strcmp(cmd, "flush") == 0) {
        int64_t ret = sys_firewall_op(FW_CMD_FLUSH, 0, 0, 0);
        if (ret == 0) puts("Firewall rules flushed.");
        else printf("nyfw: failed to flush rules (err %lld)\n", ret);
        return (int)ret;
    }

    if (strcmp(cmd, "del") == 0 || strcmp(cmd, "delete") == 0) {
        if (argc < 3) {
            puts("Usage: nyfw del <rule_id>");
            return 1;
        }
        uint32_t id = (uint32_t)atoi(argv[2]);
        int64_t ret = sys_firewall_op(FW_CMD_DEL_RULE, (uint64_t)id, 0, 0);
        if (ret == 0) printf("Rule %u deleted.\n", id);
        else printf("nyfw: failed to delete rule %u\n", id);
        return (int)ret;
    }

    if (strcmp(cmd, "policy") == 0) {
        if (argc < 4) {
            puts("Usage: nyfw policy [in|out] [accept|drop]");
            return 1;
        }
        uint8_t dir = (strcmp(argv[2], "in") == 0) ? FIREWALL_DIR_IN : FIREWALL_DIR_OUT;
        uint8_t pol = (strcmp(argv[3], "accept") == 0) ? FIREWALL_ACTION_ACCEPT : FIREWALL_ACTION_DROP;
        int64_t ret = sys_firewall_op(FW_CMD_SET_POLICY, (uint64_t)dir, (uint64_t)pol, 0);
        if (ret == 0) printf("Default %s policy set to %s.\n", argv[2], argv[3]);
        else printf("nyfw: failed to set policy\n");
        return (int)ret;
    }

    if (strcmp(cmd, "add") == 0) {
        if (argc < 4) {
            puts("Usage: nyfw add [in|out|both] [accept|drop] [proto <tcp|udp|icmp>] [src <ip>] [dst <ip>] [sport <port>] [dport <port>] [if <dev>]");
            return 1;
        }

        firewall_rule_t r;
        memset(&r, 0, sizeof(r));
        r.enabled = true;

        if (strcmp(argv[2], "in") == 0) r.direction = FIREWALL_DIR_IN;
        else if (strcmp(argv[2], "out") == 0) r.direction = FIREWALL_DIR_OUT;
        else r.direction = FIREWALL_DIR_BOTH;

        if (strcmp(argv[3], "accept") == 0) r.action = FIREWALL_ACTION_ACCEPT;
        else r.action = FIREWALL_ACTION_DROP;

        for (int i = 4; i < argc; i++) {
            if (strcmp(argv[i], "proto") == 0 && i + 1 < argc) {
                i++;
                if (strcmp(argv[i], "tcp") == 0) r.protocol = FIREWALL_PROTO_TCP;
                else if (strcmp(argv[i], "udp") == 0) r.protocol = FIREWALL_PROTO_UDP;
                else if (strcmp(argv[i], "icmp") == 0) r.protocol = FIREWALL_PROTO_ICMP;
                else if (strcmp(argv[i], "icmp6") == 0) r.protocol = FIREWALL_PROTO_ICMPV6;
            } else if (strcmp(argv[i], "src") == 0 && i + 1 < argc) {
                r.src_ip = inet_addr(argv[++i]);
                r.src_mask = 0xFFFFFFFF;
                r.family = FIREWALL_FAMILY_IPV4;
            } else if (strcmp(argv[i], "dst") == 0 && i + 1 < argc) {
                r.dst_ip = inet_addr(argv[++i]);
                r.dst_mask = 0xFFFFFFFF;
                r.family = FIREWALL_FAMILY_IPV4;
            } else if (strcmp(argv[i], "sport") == 0 && i + 1 < argc) {
                r.src_port_start = (uint16_t)atoi(argv[++i]);
                r.src_port_end = r.src_port_start;
            } else if (strcmp(argv[i], "dport") == 0 && i + 1 < argc) {
                r.dst_port_start = (uint16_t)atoi(argv[++i]);
                r.dst_port_end = r.dst_port_start;
            } else if (strcmp(argv[i], "if") == 0 && i + 1 < argc) {
                strncpy(r.ifname, argv[++i], sizeof(r.ifname) - 1);
            }
        }

        int64_t id = sys_firewall_op(FW_CMD_ADD_RULE, (uint64_t)&r, 0, 0);
        if (id > 0) printf("Added rule %lld.\n", id);
        else printf("nyfw: failed to add rule (err %lld)\n", id);
        return (id > 0) ? 0 : 1;
    }

    printf("Usage: nyfw [status|list|add|del|policy|enable|disable|flush] ...\n");
    return 1;
}
