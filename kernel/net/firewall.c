/* =============================================================================
 * Nyota OS — Network Packet-Filtering Firewall Engine (kernel/net/firewall.c)
 * Ordered ruleset, packet matching, default policies, and drop counters.
 * =========================================================================== */

#include "net/firewall.h"
#include "net/ipv4.h"
#include "net/ipv6.h"
#include "memory.h"
#include "serial.h"

static firewall_rule_t   rules[FIREWALL_MAX_RULES];
static firewall_status_t status;
static uint32_t          next_rule_id = 1;

void firewall_init(void) {
    memset(rules, 0, sizeof(rules));
    memset(&status, 0, sizeof(status));

    status.enabled = true;
    status.default_in_policy = FIREWALL_ACTION_ACCEPT;
    status.default_out_policy = FIREWALL_ACTION_ACCEPT;
    status.rule_count = 0;
    next_rule_id = 1;
}

static bool ip6_prefix_match(const uint8_t *a, const uint8_t *b, uint8_t prefix) {
    if (prefix == 0) return true;
    if (prefix > 128) prefix = 128;

    uint8_t full_bytes = prefix / 8;
    uint8_t rem_bits = prefix % 8;

    for (uint8_t i = 0; i < full_bytes; i++) {
        if (a[i] != b[i]) return false;
    }

    if (rem_bits > 0) {
        uint8_t mask = (uint8_t)(0xFF << (8 - rem_bits));
        if ((a[full_bytes] & mask) != (b[full_bytes] & mask)) {
            return false;
        }
    }

    return true;
}

static int firewall_evaluate(net_device_t *dev, uint8_t dir, uint8_t family, uint8_t proto,
                             const void *src, const void *dst,
                             uint16_t sport, uint16_t dport, size_t len) {
    if (!status.enabled) {
        return FIREWALL_ACTION_ACCEPT;
    }

    if (dir == FIREWALL_DIR_IN) {
        status.total_in_packets++;
    } else {
        status.total_out_packets++;
    }

    uint32_t src_ip4 = (family == FIREWALL_FAMILY_IPV4 && src) ? *(const uint32_t *)src : 0;
    uint32_t dst_ip4 = (family == FIREWALL_FAMILY_IPV4 && dst) ? *(const uint32_t *)dst : 0;
    const uint8_t *src_ip6 = (family == FIREWALL_FAMILY_IPV6 && src) ? (const uint8_t *)src : NULL;
    const uint8_t *dst_ip6 = (family == FIREWALL_FAMILY_IPV6 && dst) ? (const uint8_t *)dst : NULL;

    for (int i = 0; i < FIREWALL_MAX_RULES; i++) {
        firewall_rule_t *r = &rules[i];
        if (!r->enabled) continue;

        /* 1. Direction check */
        if (r->direction != FIREWALL_DIR_BOTH && r->direction != dir) {
            continue;
        }

        /* 2. Interface check */
        if (r->ifname[0] != '\0' && dev) {
            if (strcmp(r->ifname, dev->name) != 0) {
                continue;
            }
        }

        /* 3. Family check */
        if (r->family != FIREWALL_FAMILY_ANY && r->family != family) {
            continue;
        }

        /* 4. Protocol check */
        if (r->protocol != FIREWALL_PROTO_ANY && r->protocol != proto) {
            continue;
        }

        /* 5. Address check */
        if (family == FIREWALL_FAMILY_IPV4) {
            if (r->src_ip != 0 && r->src_mask != 0) {
                if ((src_ip4 & r->src_mask) != (r->src_ip & r->src_mask)) {
                    continue;
                }
            }
            if (r->dst_ip != 0 && r->dst_mask != 0) {
                if ((dst_ip4 & r->dst_mask) != (r->dst_ip & r->dst_mask)) {
                    continue;
                }
            }
        } else if (family == FIREWALL_FAMILY_IPV6 && src_ip6 && dst_ip6) {
            if (r->src_ip6_prefix > 0) {
                if (!ip6_prefix_match(src_ip6, r->src_ip6, r->src_ip6_prefix)) {
                    continue;
                }
            }
            if (r->dst_ip6_prefix > 0) {
                if (!ip6_prefix_match(dst_ip6, r->dst_ip6, r->dst_ip6_prefix)) {
                    continue;
                }
            }
        }

        /* 6. Port checks (applicable for TCP / UDP) */
        if (proto == FIREWALL_PROTO_TCP || proto == FIREWALL_PROTO_UDP) {
            if (r->src_port_start > 0) {
                uint16_t end = (r->src_port_end > 0) ? r->src_port_end : r->src_port_start;
                if (sport < r->src_port_start || sport > end) {
                    continue;
                }
            }
            if (r->dst_port_start > 0) {
                uint16_t end = (r->dst_port_end > 0) ? r->dst_port_end : r->dst_port_start;
                if (dport < r->dst_port_start || dport > end) {
                    continue;
                }
            }
        }

        /* Match found! First match wins */
        r->packets_matched++;
        r->bytes_matched += len;

        if (r->action == FIREWALL_ACTION_DROP) {
            status.total_dropped++;
            return FIREWALL_ACTION_DROP;
        } else {
            status.total_accepted++;
            return FIREWALL_ACTION_ACCEPT;
        }
    }

    /* Default Policy */
    uint8_t def = (dir == FIREWALL_DIR_IN) ? status.default_in_policy : status.default_out_policy;
    if (def == FIREWALL_ACTION_DROP) {
        status.total_dropped++;
        return FIREWALL_ACTION_DROP;
    } else {
        status.total_accepted++;
        return FIREWALL_ACTION_ACCEPT;
    }
}

int firewall_check_in(net_device_t *dev, uint8_t family, uint8_t proto,
                      const void *src, const void *dst,
                      uint16_t sport, uint16_t dport, size_t len) {
    return firewall_evaluate(dev, FIREWALL_DIR_IN, family, proto, src, dst, sport, dport, len);
}

int firewall_check_out(net_device_t *dev, uint8_t family, uint8_t proto,
                       const void *src, const void *dst,
                       uint16_t sport, uint16_t dport, size_t len) {
    return firewall_evaluate(dev, FIREWALL_DIR_OUT, family, proto, src, dst, sport, dport, len);
}

int firewall_add_rule(const firewall_rule_t *rule) {
    if (!rule) return -1;

    for (int i = 0; i < FIREWALL_MAX_RULES; i++) {
        if (!rules[i].enabled) {
            rules[i] = *rule;
            rules[i].id = next_rule_id++;
            rules[i].enabled = true;
            rules[i].packets_matched = 0;
            rules[i].bytes_matched = 0;
            status.rule_count++;
            return (int)rules[i].id;
        }
    }
    return -1; /* Ruleset full */
}

int firewall_del_rule(uint32_t id) {
    for (int i = 0; i < FIREWALL_MAX_RULES; i++) {
        if (rules[i].enabled && rules[i].id == id) {
            rules[i].enabled = false;
            status.rule_count--;
            return 0;
        }
    }
    return -1; /* Not found */
}

int firewall_get_status(firewall_status_t *out_status) {
    if (!out_status) return -1;
    *out_status = status;
    return 0;
}

int firewall_get_rules(firewall_rule_t *out_rules, size_t max_rules, size_t *out_count) {
    if (!out_rules || !out_count) return -1;

    size_t count = 0;
    for (int i = 0; i < FIREWALL_MAX_RULES && count < max_rules; i++) {
        if (rules[i].enabled) {
            out_rules[count++] = rules[i];
        }
    }

    *out_count = count;
    return 0;
}

int firewall_set_policy(uint8_t dir, uint8_t policy) {
    if (policy != FIREWALL_ACTION_ACCEPT && policy != FIREWALL_ACTION_DROP) {
        return -1;
    }
    if (dir == FIREWALL_DIR_IN || dir == FIREWALL_DIR_BOTH) {
        status.default_in_policy = policy;
    }
    if (dir == FIREWALL_DIR_OUT || dir == FIREWALL_DIR_BOTH) {
        status.default_out_policy = policy;
    }
    return 0;
}

int firewall_set_enabled(bool enabled) {
    status.enabled = enabled;
    return 0;
}

int firewall_flush(void) {
    memset(rules, 0, sizeof(rules));
    status.rule_count = 0;
    return 0;
}
