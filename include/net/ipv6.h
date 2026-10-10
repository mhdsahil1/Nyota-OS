/* =============================================================================
 * Nyota OS — Internet Protocol Version 6 (IPv6) Interface
 * RFC 8200 packet header, 128-bit address representation, SLAAC, and formatting.
 * =========================================================================== */

#ifndef NYOTA_NET_IPV6_H
#define NYOTA_NET_IPV6_H

#include "types.h"
#include "net/netdev.h"

#define IPV6_ADDR_LEN       16
#define IPV6_HLEN           40
#define IPV6_DEFAULT_HOP    64

#define IP_PROTO_ICMPV6     58
#define IP_PROTO_TCP        6
#define IP_PROTO_UDP        17
#define IP_PROTO_NONE       59

/* 128-bit IPv6 Address */
typedef struct {
    uint8_t bytes[16];
} ipv6_addr_t;

/* IPv6 Fixed Header (40 octets, RFC 8200) */
typedef struct __attribute__((packed)) {
    uint32_t vtc_flow;      /* Version (4), Traffic Class (8), Flow Label (20) */
    uint16_t payload_len;   /* Length of payload (following this header) */
    uint8_t  next_header;   /* Protocol of next header (TCP, UDP, ICMPv6, etc.) */
    uint8_t  hop_limit;     /* Hop limit / TTL */
    ipv6_addr_t src;        /* Source IPv6 Address */
    ipv6_addr_t dest;       /* Destination IPv6 Address */
} ipv6_header_t;

/* Standard IPv6 Addresses */
extern const ipv6_addr_t IPV6_ADDR_ANY;
extern const ipv6_addr_t IPV6_ADDR_LOOPBACK;

/* IPv6 Address Operations */
bool ipv6_equal(const ipv6_addr_t *a, const ipv6_addr_t *b);
bool ipv6_is_any(const ipv6_addr_t *addr);
bool ipv6_is_loopback(const ipv6_addr_t *addr);
bool ipv6_is_multicast(const ipv6_addr_t *addr);
bool ipv6_is_linklocal(const ipv6_addr_t *addr);

/* EUI-64 SLAAC Generation & Multicast Mapping */
void ipv6_generate_slaac(const mac_addr_t mac, ipv6_addr_t *out_addr);
void ipv6_solicited_node_multicast(const ipv6_addr_t *addr, ipv6_addr_t *out_mcast);
void ipv6_multicast_mac(const ipv6_addr_t *mcast, mac_addr_t out_mac);

/* String Parsing & Formatting */
int  ipv6_parse(const char *str, ipv6_addr_t *addr);
void ipv6_format(const ipv6_addr_t *addr, char *buf, size_t buf_len);

/* Checksum Helper */
uint16_t ipv6_pseudo_checksum(const ipv6_addr_t *src, const ipv6_addr_t *dest,
                              uint8_t next_header, const void *payload, size_t len);

/* IPv6 Protocol Lifecycle */
void ipv6_init(void);
int  ipv6_receive(net_device_t *dev, const void *data, size_t len);
int  ipv6_send(const ipv6_addr_t *dest, uint8_t next_header, const void *payload, size_t len);
int  ipv6_send_from(net_device_t *dev, const ipv6_addr_t *src, const ipv6_addr_t *dest,
                    uint8_t next_header, const void *payload, size_t len);

#endif /* NYOTA_NET_IPV6_H */
