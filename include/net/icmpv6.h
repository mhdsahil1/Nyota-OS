/* =============================================================================
 * Nyota OS — ICMPv6 & Neighbor Discovery Protocol (NDP) Interface
 * RFC 4443 ICMPv6 and RFC 4861 Neighbor Discovery Protocol (NDP).
 * =========================================================================== */

#ifndef NYOTA_NET_ICMPV6_H
#define NYOTA_NET_ICMPV6_H

#include "types.h"
#include "net/netdev.h"
#include "net/ipv6.h"

#define ICMPV6_TYPE_DEST_UNREACH    1
#define ICMPV6_TYPE_PACKET_TOO_BIG  2
#define ICMPV6_TYPE_TIME_EXCEEDED   3
#define ICMPV6_TYPE_PARAM_PROBLEM   4
#define ICMPV6_TYPE_ECHO_REQUEST    128
#define ICMPV6_TYPE_ECHO_REPLY      129
#define ICMPV6_TYPE_ROUTER_SOLICIT  133
#define ICMPV6_TYPE_ROUTER_ADVERT   134
#define ICMPV6_TYPE_NEIGHBOR_SOLICIT 135
#define ICMPV6_TYPE_NEIGHBOR_ADVERT  136

#define NDP_OPT_SRC_LINK_LAYER      1
#define NDP_OPT_TARGET_LINK_LAYER   2
#define NDP_OPT_PREFIX_INFO         3

#define NDP_CACHE_MAX               16

/* Generic ICMPv6 Header */
typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
} icmpv6_header_t;

/* ICMPv6 Echo Request / Reply */
typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
} icmpv6_echo_header_t;

/* NDP Neighbor Solicitation Header (RFC 4861 Section 4.3) */
typedef struct __attribute__((packed)) {
    uint8_t     type;           /* 135 */
    uint8_t     code;           /* 0 */
    uint16_t    checksum;
    uint32_t    reserved;
    ipv6_addr_t target;
} icmpv6_ns_header_t;

/* NDP Neighbor Advertisement Header (RFC 4861 Section 4.4) */
typedef struct __attribute__((packed)) {
    uint8_t     type;           /* 136 */
    uint8_t     code;           /* 0 */
    uint16_t    checksum;
    uint32_t    flags;          /* R (bit 31), S (bit 30), O (bit 29) */
    ipv6_addr_t target;
} icmpv6_na_header_t;

/* NDP Link Layer Option (Type 1 or 2) */
typedef struct __attribute__((packed)) {
    uint8_t    type;
    uint8_t    length;          /* Length in units of 8 octets (1 for 8 bytes) */
    mac_addr_t mac;
} ndp_opt_ll_t;

/* NDP Cache Entry */
typedef struct {
    ipv6_addr_t ip6;
    mac_addr_t  mac;
    uint64_t    timestamp;
    bool        valid;
} ndp_entry_t;

/* NDP Cache APIs */
void ndp_init(void);
int  ndp_lookup(const ipv6_addr_t *ip6, mac_addr_t out_mac);
void ndp_insert(const ipv6_addr_t *ip6, const mac_addr_t mac);

/* ICMPv6 Protocol Lifecycle & Dispatch */
void icmpv6_init(void);
int  icmpv6_receive(net_device_t *dev, const ipv6_addr_t *src, const ipv6_addr_t *dest,
                    const void *data, size_t len);
int  icmpv6_send_echo_request(net_device_t *dev, const ipv6_addr_t *dest,
                              uint16_t id, uint16_t seq, const void *payload, size_t len);
int  icmpv6_send_ns(net_device_t *dev, const ipv6_addr_t *target);

#endif /* NYOTA_NET_ICMPV6_H */
