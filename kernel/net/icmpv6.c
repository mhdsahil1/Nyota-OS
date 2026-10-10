/* =============================================================================
 * Nyota OS — ICMPv6 & Neighbor Discovery Protocol (NDP) Implementation
 * RFC 4443 ICMPv6 Echo Request/Reply and RFC 4861 Neighbor Discovery Protocol.
 * =========================================================================== */

#include "net/icmpv6.h"
#include "net/ipv6.h"
#include "net/ethernet.h"
#include "memory.h"
#include "serial.h"

static ndp_entry_t ndp_cache[NDP_CACHE_MAX];

/* ── Neighbor Discovery Protocol (NDP) Cache ──────────────────────────────── */

void ndp_init(void) {
    memset(ndp_cache, 0, sizeof(ndp_cache));
}

int ndp_lookup(const ipv6_addr_t *ip6, mac_addr_t out_mac) {
    if (!ip6 || !out_mac) return -1;

    for (int i = 0; i < NDP_CACHE_MAX; i++) {
        if (ndp_cache[i].valid && ipv6_equal(&ndp_cache[i].ip6, ip6)) {
            memcpy(out_mac, ndp_cache[i].mac, ETH_ALEN);
            return 0;
        }
    }
    return -1;
}

void ndp_insert(const ipv6_addr_t *ip6, const mac_addr_t mac) {
    if (!ip6 || !mac) return;

    /* Check if already present */
    for (int i = 0; i < NDP_CACHE_MAX; i++) {
        if (ndp_cache[i].valid && ipv6_equal(&ndp_cache[i].ip6, ip6)) {
            memcpy(ndp_cache[i].mac, mac, ETH_ALEN);
            return;
        }
    }

    /* Find empty or first slot */
    for (int i = 0; i < NDP_CACHE_MAX; i++) {
        if (!ndp_cache[i].valid) {
            memcpy(&ndp_cache[i].ip6, ip6, sizeof(ipv6_addr_t));
            memcpy(ndp_cache[i].mac, mac, ETH_ALEN);
            ndp_cache[i].valid = true;
            return;
        }
    }

    /* Replace slot 0 */
    memcpy(&ndp_cache[0].ip6, ip6, sizeof(ipv6_addr_t));
    memcpy(ndp_cache[0].mac, mac, ETH_ALEN);
    ndp_cache[0].valid = true;
}

/* ── ICMPv6 Protocol Lifecycle & Dispatch ─────────────────────────────────── */

void icmpv6_init(void) {
    ndp_init();
}

int icmpv6_receive(net_device_t *dev, const ipv6_addr_t *src, const ipv6_addr_t *dest,
                    const void *data, size_t len) {
    if (!dev || !src || !dest || !data || len < sizeof(icmpv6_header_t)) {
        return -1;
    }

    /* 1. Validate ICMPv6 Pseudo-Header Checksum */
    if (ipv6_pseudo_checksum(src, dest, IP_PROTO_ICMPV6, data, len) != 0) {
        return -1; /* Checksum mismatch */
    }

    const icmpv6_header_t *hdr = (const icmpv6_header_t *)data;

    switch (hdr->type) {
        case ICMPV6_TYPE_ECHO_REQUEST: {
            if (len < sizeof(icmpv6_echo_header_t)) return -1;

            /* Construct Echo Reply */
            uint8_t reply_buf[1500];
            if (len > sizeof(reply_buf)) return -1;
            memcpy(reply_buf, data, len);

            icmpv6_echo_header_t *rep = (icmpv6_echo_header_t *)reply_buf;
            rep->type = ICMPV6_TYPE_ECHO_REPLY;
            rep->code = 0;
            rep->checksum = 0;

            const ipv6_addr_t *reply_src = dest;
            if (ipv6_is_multicast(dest)) {
                reply_src = (const ipv6_addr_t *)dev->ip6_addr;
            }

            rep->checksum = ipv6_pseudo_checksum(reply_src, src, IP_PROTO_ICMPV6, reply_buf, len);
            return ipv6_send_from(dev, reply_src, src, IP_PROTO_ICMPV6, reply_buf, len);
        }

        case ICMPV6_TYPE_NEIGHBOR_SOLICIT: {
            if (len < sizeof(icmpv6_ns_header_t)) return -1;
            const icmpv6_ns_header_t *ns = (const icmpv6_ns_header_t *)data;

            /* Check if target is this device's IPv6 address */
            if (!dev->has_ip6 || !ipv6_equal(&ns->target, (const ipv6_addr_t *)dev->ip6_addr)) {
                return 0; /* Target is not us */
            }

            /* Learn sender MAC if source link-layer option is present */
            if (len >= sizeof(icmpv6_ns_header_t) + sizeof(ndp_opt_ll_t)) {
                const ndp_opt_ll_t *opt = (const ndp_opt_ll_t *)((const uint8_t *)data + sizeof(icmpv6_ns_header_t));
                if (opt->type == NDP_OPT_SRC_LINK_LAYER && opt->length == 1) {
                    ndp_insert(src, opt->mac);
                }
            }

            /* Send Neighbor Advertisement (NA) */
            uint8_t na_buf[sizeof(icmpv6_na_header_t) + sizeof(ndp_opt_ll_t)];
            memset(na_buf, 0, sizeof(na_buf));

            icmpv6_na_header_t *na = (icmpv6_na_header_t *)na_buf;
            na->type = ICMPV6_TYPE_NEIGHBOR_ADVERT;
            na->code = 0;
            na->checksum = 0;
            /* Flags: Solicited (0x40000000) | Override (0x20000000) */
            na->flags = htonl(0x60000000U);
            memcpy(&na->target, dev->ip6_addr, sizeof(ipv6_addr_t));

            ndp_opt_ll_t *ll = (ndp_opt_ll_t *)(na_buf + sizeof(icmpv6_na_header_t));
            ll->type = NDP_OPT_TARGET_LINK_LAYER;
            ll->length = 1; /* 8 octets */
            memcpy(ll->mac, dev->mac, ETH_ALEN);

            const ipv6_addr_t *na_src = (const ipv6_addr_t *)dev->ip6_addr;
            const ipv6_addr_t *na_dst = src;
            ipv6_addr_t all_nodes_mcast;

            if (ipv6_is_any(src)) {
                /* If NS came from unspecified address (DAD), multicast NA to all-nodes */
                memset(&all_nodes_mcast, 0, sizeof(ipv6_addr_t));
                all_nodes_mcast.bytes[0] = 0xFF;
                all_nodes_mcast.bytes[1] = 0x02;
                all_nodes_mcast.bytes[15] = 0x01;
                na_dst = &all_nodes_mcast;
                na->flags = htonl(0x20000000U); /* Override only, not solicited */
            }

            na->checksum = ipv6_pseudo_checksum(na_src, na_dst, IP_PROTO_ICMPV6, na_buf, sizeof(na_buf));
            return ipv6_send_from(dev, na_src, na_dst, IP_PROTO_ICMPV6, na_buf, sizeof(na_buf));
        }

        case ICMPV6_TYPE_NEIGHBOR_ADVERT: {
            if (len < sizeof(icmpv6_na_header_t)) return -1;
            const icmpv6_na_header_t *na = (const icmpv6_na_header_t *)data;

            if (len >= sizeof(icmpv6_na_header_t) + sizeof(ndp_opt_ll_t)) {
                const ndp_opt_ll_t *opt = (const ndp_opt_ll_t *)((const uint8_t *)data + sizeof(icmpv6_na_header_t));
                if (opt->type == NDP_OPT_TARGET_LINK_LAYER && opt->length == 1) {
                    ndp_insert(&na->target, opt->mac);
                }
            }
            return 0;
        }

        default:
            return 0;
    }
}

int icmpv6_send_ns(net_device_t *dev, const ipv6_addr_t *target) {
    if (!dev || !target || !dev->has_ip6) return -1;

    uint8_t ns_buf[sizeof(icmpv6_ns_header_t) + sizeof(ndp_opt_ll_t)];
    memset(ns_buf, 0, sizeof(ns_buf));

    icmpv6_ns_header_t *ns = (icmpv6_ns_header_t *)ns_buf;
    ns->type = ICMPV6_TYPE_NEIGHBOR_SOLICIT;
    ns->code = 0;
    ns->checksum = 0;
    ns->reserved = 0;
    memcpy(&ns->target, target, sizeof(ipv6_addr_t));

    ndp_opt_ll_t *ll = (ndp_opt_ll_t *)(ns_buf + sizeof(icmpv6_ns_header_t));
    ll->type = NDP_OPT_SRC_LINK_LAYER;
    ll->length = 1;
    memcpy(ll->mac, dev->mac, ETH_ALEN);

    ipv6_addr_t mcast;
    ipv6_solicited_node_multicast(target, &mcast);

    const ipv6_addr_t *src = (const ipv6_addr_t *)dev->ip6_addr;
    ns->checksum = ipv6_pseudo_checksum(src, &mcast, IP_PROTO_ICMPV6, ns_buf, sizeof(ns_buf));

    return ipv6_send_from(dev, src, &mcast, IP_PROTO_ICMPV6, ns_buf, sizeof(ns_buf));
}

int icmpv6_send_echo_request(net_device_t *dev, const ipv6_addr_t *dest,
                              uint16_t id, uint16_t seq, const void *payload, size_t len) {
    if (!dev || !dest || !dev->has_ip6) return -1;

    size_t total_len = sizeof(icmpv6_echo_header_t) + len;
    uint8_t buffer[1500];
    if (total_len > sizeof(buffer)) return -1;

    icmpv6_echo_header_t *echo = (icmpv6_echo_header_t *)buffer;
    echo->type = ICMPV6_TYPE_ECHO_REQUEST;
    echo->code = 0;
    echo->checksum = 0;
    echo->id = htons(id);
    echo->sequence = htons(seq);

    if (payload && len > 0) {
        memcpy(buffer + sizeof(icmpv6_echo_header_t), payload, len);
    }

    const ipv6_addr_t *src = (const ipv6_addr_t *)dev->ip6_addr;
    echo->checksum = ipv6_pseudo_checksum(src, dest, IP_PROTO_ICMPV6, buffer, total_len);

    return ipv6_send_from(dev, src, dest, IP_PROTO_ICMPV6, buffer, total_len);
}
