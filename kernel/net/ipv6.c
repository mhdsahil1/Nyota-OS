/* =============================================================================
 * Nyota OS — Internet Protocol Version 6 (IPv6) Implementation
 * RFC 8200 packet parsing, SLAAC, multicast mapping, checksum, and dispatch.
 * =========================================================================== */

#include "net/ipv6.h"
#include "net/icmpv6.h"
#include "net/ethernet.h"
#include "net/ipv4.h"
#include "net/firewall.h"
#include "memory.h"
#include "heap.h"
#include "serial.h"

const ipv6_addr_t IPV6_ADDR_ANY = { .bytes = {0} };
const ipv6_addr_t IPV6_ADDR_LOOPBACK = {
    .bytes = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}
};

/* ── Address Comparison & Predicates ──────────────────────────────────────── */

bool ipv6_equal(const ipv6_addr_t *a, const ipv6_addr_t *b) {
    if (!a || !b) return false;
    for (int i = 0; i < IPV6_ADDR_LEN; i++) {
        if (a->bytes[i] != b->bytes[i]) return false;
    }
    return true;
}

bool ipv6_is_any(const ipv6_addr_t *addr) {
    return ipv6_equal(addr, &IPV6_ADDR_ANY);
}

bool ipv6_is_loopback(const ipv6_addr_t *addr) {
    return ipv6_equal(addr, &IPV6_ADDR_LOOPBACK);
}

bool ipv6_is_multicast(const ipv6_addr_t *addr) {
    if (!addr) return false;
    return addr->bytes[0] == 0xFF;
}

bool ipv6_is_linklocal(const ipv6_addr_t *addr) {
    if (!addr) return false;
    return (addr->bytes[0] == 0xFE) && ((addr->bytes[1] & 0xC0) == 0x80);
}

/* ── SLAAC & Multicast Mapping (RFC 4291 / RFC 2464) ──────────────────────── */

void ipv6_generate_slaac(const mac_addr_t mac, ipv6_addr_t *out_addr) {
    if (!out_addr) return;
    memset(out_addr, 0, sizeof(ipv6_addr_t));

    /* Link-Local Prefix: fe80::/64 */
    out_addr->bytes[0] = 0xFE;
    out_addr->bytes[1] = 0x80;

    /* Modified EUI-64: invert universal/local bit of first byte */
    out_addr->bytes[8]  = mac[0] ^ 0x02;
    out_addr->bytes[9]  = mac[1];
    out_addr->bytes[10] = mac[2];
    out_addr->bytes[11] = 0xFF;
    out_addr->bytes[12] = 0xFE;
    out_addr->bytes[13] = mac[3];
    out_addr->bytes[14] = mac[4];
    out_addr->bytes[15] = mac[5];
}

void ipv6_solicited_node_multicast(const ipv6_addr_t *addr, ipv6_addr_t *out_mcast) {
    if (!addr || !out_mcast) return;
    memset(out_mcast, 0, sizeof(ipv6_addr_t));

    /* ff02::1:ffxx:xxxx */
    out_mcast->bytes[0]  = 0xFF;
    out_mcast->bytes[1]  = 0x02;
    out_mcast->bytes[11] = 0x01;
    out_mcast->bytes[12] = 0xFF;
    out_mcast->bytes[13] = addr->bytes[13];
    out_mcast->bytes[14] = addr->bytes[14];
    out_mcast->bytes[15] = addr->bytes[15];
}

void ipv6_multicast_mac(const ipv6_addr_t *mcast, mac_addr_t out_mac) {
    if (!mcast || !out_mac) return;
    /* RFC 2464 Section 7: 33-33-xx-xx-xx-xx */
    out_mac[0] = 0x33;
    out_mac[1] = 0x33;
    out_mac[2] = mcast->bytes[12];
    out_mac[3] = mcast->bytes[13];
    out_mac[4] = mcast->bytes[14];
    out_mac[5] = mcast->bytes[15];
}

/* ── String Parsing & Formatting ──────────────────────────────────────────── */

static void uint16_to_hex(uint16_t val, char *buf, size_t *pos) {
    const char hex_chars[] = "0123456789abcdef";
    bool started = false;

    for (int i = 3; i >= 0; i--) {
        uint8_t nib = (val >> (i * 4)) & 0xF;
        if (nib != 0 || started || i == 0) {
            buf[(*pos)++] = hex_chars[nib];
            started = true;
        }
    }
}

void ipv6_format(const ipv6_addr_t *addr, char *buf, size_t buf_len) {
    if (!addr || !buf || buf_len < 40) return;

    size_t pos = 0;
    for (int i = 0; i < 8; i++) {
        uint16_t word = ((uint16_t)addr->bytes[i * 2] << 8) | addr->bytes[i * 2 + 1];
        uint16_to_hex(word, buf, &pos);
        if (i < 7) {
            buf[pos++] = ':';
        }
    }
    buf[pos] = '\0';
}

int ipv6_parse(const char *str, ipv6_addr_t *addr) {
    if (!str || !addr) return -1;
    memset(addr, 0, sizeof(ipv6_addr_t));

    uint16_t words[8] = {0};
    int word_count = 0;
    int double_colon_pos = -1;

    const char *p = str;
    if (p[0] == ':' && p[1] == ':') {
        double_colon_pos = 0;
        p += 2;
    }

    while (*p && word_count < 8) {
        uint32_t val = 0;
        int hex_digits = 0;

        while (*p && *p != ':') {
            char c = *p++;
            int v = -1;
            if (c >= '0' && c <= '9') v = c - '0';
            else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
            else return -1;

            val = (val << 4) | (uint32_t)v;
            hex_digits++;
            if (hex_digits > 4) return -1;
        }

        words[word_count++] = (uint16_t)val;

        if (*p == ':') {
            p++;
            if (*p == ':') {
                if (double_colon_pos != -1) return -1; /* Duplicate :: */
                double_colon_pos = word_count;
                p++;
                if (*p == '\0') break;
            }
        }
    }

    if (double_colon_pos != -1) {
        int fill_count = 8 - word_count;
        int out_idx = 7;
        for (int i = word_count - 1; i >= double_colon_pos; i--) {
            words[out_idx--] = words[i];
        }
        for (int i = 0; i < fill_count; i++) {
            words[double_colon_pos + i] = 0;
        }
    } else if (word_count != 8) {
        return -1;
    }

    for (int i = 0; i < 8; i++) {
        addr->bytes[i * 2]     = (uint8_t)(words[i] >> 8);
        addr->bytes[i * 2 + 1] = (uint8_t)(words[i] & 0xFF);
    }

    return 0;
}

/* ── Checksum Calculation (RFC 8200 Section 8.1) ──────────────────────────── */

uint16_t ipv6_pseudo_checksum(const ipv6_addr_t *src, const ipv6_addr_t *dest,
                              uint8_t next_header, const void *payload, size_t len) {
    uint32_t sum = 0;

    /* 1. Source IPv6 (16 bytes) */
    sum = net_checksum_accum(src->bytes, 16, sum);

    /* 2. Destination IPv6 (16 bytes) */
    sum = net_checksum_accum(dest->bytes, 16, sum);

    /* 3. Upper-layer packet length (uint32_t network byte order) */
    uint32_t net_len = htonl((uint32_t)len);
    sum = net_checksum_accum(&net_len, 4, sum);

    /* 4. Next Header (3 bytes zero, 1 byte next_header) */
    uint8_t proto_pad[4] = {0, 0, 0, next_header};
    sum = net_checksum_accum(proto_pad, 4, sum);

    /* 5. Payload */
    if (payload && len > 0) {
        sum = net_checksum_accum(payload, len, sum);
    }

    return net_checksum_finish(sum);
}

/* ── Lifecycle & Protocol Dispatch ────────────────────────────────────────── */

void ipv6_init(void) {
    ndp_init();
    icmpv6_init();
}

int ipv6_receive(net_device_t *dev, const void *data, size_t len) {
    if (!dev || !data || len < sizeof(ipv6_header_t)) {
        return -1;
    }

    const ipv6_header_t *ip6 = (const ipv6_header_t *)data;

    /* 1. Version check (top 4 bits of vtc_flow) */
    uint32_t vtc = ntohl(ip6->vtc_flow);
    uint8_t version = (uint8_t)(vtc >> 28);
    if (version != 6) {
        return -1;
    }

    /* 2. Payload length check */
    size_t payload_len = ntohs(ip6->payload_len);
    if (sizeof(ipv6_header_t) + payload_len > len) {
        return -1; /* Malformed packet length */
    }

    /* 3. Hop limit check */
    if (ip6->hop_limit == 0) {
        return 0; /* Hop limit exceeded, silently drop */
    }

    /* 4. Destination validation: check if for our interface or supported multicast */
    bool for_us = false;
    if (dev->has_ip6 && ipv6_equal(&ip6->dest, (const ipv6_addr_t *)dev->ip6_addr)) {
        for_us = true;
    } else if (ipv6_is_loopback(&ip6->dest)) {
        for_us = true;
    } else if (ipv6_is_multicast(&ip6->dest)) {
        /* Accept all-nodes ff02::1 or solicited-node multicast for dev */
        if (ip6->dest.bytes[1] == 0x02) {
            if (ip6->dest.bytes[15] == 1 && ip6->dest.bytes[14] == 0) {
                for_us = true; /* ff02::1 */
            } else if (dev->has_ip6) {
                ipv6_addr_t sol;
                ipv6_solicited_node_multicast((const ipv6_addr_t *)dev->ip6_addr, &sol);
                if (ipv6_equal(&ip6->dest, &sol)) {
                    for_us = true;
                }
            }
        }
    }

    if (!for_us) {
        return 0; /* Not addressed to us */
    }

    const uint8_t *payload = (const uint8_t *)data + sizeof(ipv6_header_t);

    /* 5. Firewall inspection */
    uint16_t sport = 0, dport = 0;
    if (ip6->next_header == IP_PROTO_TCP || ip6->next_header == IP_PROTO_UDP) {
        if (payload_len >= 4) {
            sport = (payload[0] << 8) | payload[1];
            dport = (payload[2] << 8) | payload[3];
        }
    }

    int fw_action = firewall_check_in(dev, FIREWALL_FAMILY_IPV6, ip6->next_header,
                                      &ip6->src, &ip6->dest, sport, dport, payload_len);
    if (fw_action == FIREWALL_ACTION_DROP) {
        dev->rx_dropped++;
        return 0;
    }

    /* 6. Protocol demultiplexing */
    switch (ip6->next_header) {
        case IP_PROTO_ICMPV6:
            return icmpv6_receive(dev, &ip6->src, &ip6->dest, payload, payload_len);

        default:
            return 0; /* Unsupported IPv6 transport */
    }
}

int ipv6_send_from(net_device_t *dev, const ipv6_addr_t *src, const ipv6_addr_t *dest,
                    uint8_t next_header, const void *payload, size_t len) {
    if (!dev || !src || !dest || (!payload && len > 0)) return -1;

    size_t total_len = sizeof(ipv6_header_t) + len;
    if (total_len > dev->mtu) {
        return -1;
    }

    /* Firewall inspection */
    uint16_t sport = 0, dport = 0;
    if (next_header == IP_PROTO_TCP || next_header == IP_PROTO_UDP) {
        if (len >= 4 && payload) {
            const uint8_t *p = (const uint8_t *)payload;
            sport = (p[0] << 8) | p[1];
            dport = (p[2] << 8) | p[3];
        }
    }

    int fw_action = firewall_check_out(dev, FIREWALL_FAMILY_IPV6, next_header,
                                       src, dest, sport, dport, len);
    if (fw_action == FIREWALL_ACTION_DROP) {
        dev->tx_dropped++;
        return -1;
    }

    uint8_t buffer[ETH_FRAME_LEN];
    uint8_t *send_buf = buffer;
    bool dyn_alloc = false;

    if (total_len > sizeof(buffer)) {
        send_buf = (uint8_t *)kmalloc(total_len);
        if (!send_buf) return -1;
        dyn_alloc = true;
    }

    ipv6_header_t *ip6 = (ipv6_header_t *)send_buf;
    ip6->vtc_flow = htonl(0x60000000U); /* Version 6, Traffic Class 0, Flow 0 */
    ip6->payload_len = htons((uint16_t)len);
    ip6->next_header = next_header;
    ip6->hop_limit = IPV6_DEFAULT_HOP;
    memcpy(&ip6->src, src, sizeof(ipv6_addr_t));
    memcpy(&ip6->dest, dest, sizeof(ipv6_addr_t));

    if (payload && len > 0) {
        memcpy(send_buf + sizeof(ipv6_header_t), payload, len);
    }

    int ret = 0;

    if (strcmp(dev->name, "lo") == 0) {
        /* Loopback directly receives its own transmitted packet */
        ret = ipv6_receive(dev, send_buf, total_len);
    } else {
        mac_addr_t dest_mac;

        if (ipv6_is_multicast(dest)) {
            ipv6_multicast_mac(dest, dest_mac);
        } else {
            /* Unicast: resolve MAC via NDP cache */
            if (ndp_lookup(dest, dest_mac) != 0) {
                /* Not in cache: trigger Neighbor Solicitation and use broadcast as fallback */
                icmpv6_send_ns(dev, dest);
                memcpy(dest_mac, ETH_BROADCAST_MAC, ETH_ALEN);
            }
        }

        ret = ethernet_send(dev, dest_mac, ETHERTYPE_IPV6, send_buf, total_len);
    }

    if (dyn_alloc) {
        kfree(send_buf);
    }

    return ret;
}

int ipv6_send(const ipv6_addr_t *dest, uint8_t next_header, const void *payload, size_t len) {
    if (!dest) return -1;

    net_device_t *dev = NULL;
    if (ipv6_is_loopback(dest)) {
        dev = netdev_get_by_name("lo");
    } else {
        dev = netdev_get_default();
    }

    if (!dev || !dev->has_ip6) return -1;

    return ipv6_send_from(dev, (const ipv6_addr_t *)dev->ip6_addr, dest, next_header, payload, len);
}
