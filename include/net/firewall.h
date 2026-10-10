/* =============================================================================
 * Nyota OS — Network Packet-Filtering Firewall (nyfw) Interface
 * State inspection, packet filtering engine, ordered ruleset, and statistics.
 * =========================================================================== */

#ifndef NYOTA_NET_FIREWALL_H
#define NYOTA_NET_FIREWALL_H

#include "types.h"
#include "net/netdev.h"

#define FIREWALL_MAX_RULES          32

#define FIREWALL_DIR_IN             1
#define FIREWALL_DIR_OUT            2
#define FIREWALL_DIR_BOTH           3

#define FIREWALL_ACTION_DROP        0
#define FIREWALL_ACTION_ACCEPT      1

#define FIREWALL_FAMILY_ANY         0
#define FIREWALL_FAMILY_IPV4        1
#define FIREWALL_FAMILY_IPV6        2

#define FIREWALL_PROTO_ANY          0
#define FIREWALL_PROTO_ICMP         1
#define FIREWALL_PROTO_TCP          6
#define FIREWALL_PROTO_UDP          17
#define FIREWALL_PROTO_ICMPV6       58

/* Firewall Rule Definition */
typedef struct {
    uint32_t id;
    uint8_t  direction;             /* FIREWALL_DIR_IN, OUT, BOTH */
    uint8_t  action;                /* FIREWALL_ACTION_DROP, ACCEPT */
    uint8_t  family;                /* FIREWALL_FAMILY_ANY, IPV4, IPV6 */
    uint8_t  protocol;              /* FIREWALL_PROTO_ANY, TCP, UDP, ICMP, ICMPV6 */

    /* IPv4 Criteria */
    uint32_t src_ip;
    uint32_t src_mask;
    uint32_t dst_ip;
    uint32_t dst_mask;

    /* IPv6 Criteria */
    uint8_t  src_ip6[16];
    uint8_t  src_ip6_prefix;        /* 0-128 */
    uint8_t  dst_ip6[16];
    uint8_t  dst_ip6_prefix;        /* 0-128 */

    /* Transport Port Ranges (0 = wildcard) */
    uint16_t src_port_start;
    uint16_t src_port_end;
    uint16_t dst_port_start;
    uint16_t dst_port_end;

    char     ifname[16];            /* Target interface or empty for any */

    /* Rule Match Statistics */
    uint64_t packets_matched;
    uint64_t bytes_matched;
    bool     enabled;
} firewall_rule_t;

/* Firewall Overall Status */
typedef struct {
    bool     enabled;
    uint8_t  default_in_policy;     /* ACCEPT or DROP */
    uint8_t  default_out_policy;    /* ACCEPT or DROP */
    uint32_t rule_count;
    uint64_t total_in_packets;
    uint64_t total_out_packets;
    uint64_t total_accepted;
    uint64_t total_dropped;
} firewall_status_t;

/* Syscall Commands for SYS_FIREWALL_OP */
#define FW_CMD_GET_STATUS           1
#define FW_CMD_GET_RULES            2
#define FW_CMD_ADD_RULE             3
#define FW_CMD_DEL_RULE             4
#define FW_CMD_SET_POLICY           5
#define FW_CMD_SET_ENABLED          6
#define FW_CMD_FLUSH                7

/* Kernel APIs */
void firewall_init(void);
int  firewall_check_in(net_device_t *dev, uint8_t family, uint8_t proto,
                       const void *src, const void *dst,
                       uint16_t sport, uint16_t dport, size_t len);
int  firewall_check_out(net_device_t *dev, uint8_t family, uint8_t proto,
                        const void *src, const void *dst,
                        uint16_t sport, uint16_t dport, size_t len);

int  firewall_add_rule(const firewall_rule_t *rule);
int  firewall_del_rule(uint32_t id);
int  firewall_get_status(firewall_status_t *out_status);
int  firewall_get_rules(firewall_rule_t *rules, size_t max_rules, size_t *out_count);
int  firewall_set_policy(uint8_t dir, uint8_t policy);
int  firewall_set_enabled(bool enabled);
int  firewall_flush(void);

#endif /* NYOTA_NET_FIREWALL_H */
