/* =============================================================================
 * Nyota OS — Internet Protocol Version 4 (IPv4) Interface
 * =========================================================================== */

#ifndef NYOTA_NET_IPV4_H
#define NYOTA_NET_IPV4_H

#include "types.h"
#include "net/netdev.h"

#define IP_PROTO_ICMP       1
#define IP_PROTO_TCP        6
#define IP_PROTO_UDP        17

#define IPV4_DEFAULT_TTL    64
#define IPV4_MIN_HLEN       20

typedef struct __attribute__((packed)) {
    uint8_t  version_ihl;   /* Version (4 bits) + Internet Header Length (4 bits) */
    uint8_t  tos;           /* Type of Service */
    uint16_t total_length;  /* Total Length (Header + Payload) */
    uint16_t id;            /* Identification */
    uint16_t flags_frag;    /* Flags (3 bits) + Fragment Offset (13 bits) */
    uint8_t  ttl;           /* Time to Live */
    uint8_t  protocol;      /* Next level protocol (ICMP, TCP, UDP) */
    uint16_t checksum;      /* Header Checksum */
    uint32_t src;           /* Source IPv4 Address */
    uint32_t dest;          /* Destination IPv4 Address */
} ipv4_header_t;

/* Internet Checksum (RFC 1071) */
uint16_t net_checksum(const void *data, size_t len);
uint32_t net_checksum_accum(const void *data, size_t len, uint32_t sum);
uint16_t net_checksum_finish(uint32_t sum);

void ipv4_init(void);
int  ipv4_receive(net_device_t *dev, const void *data, size_t len);
int  ipv4_send(uint32_t dest_ip, uint8_t protocol, const void *payload, size_t len);
int  ipv4_send_from(net_device_t *dev, uint32_t src_ip, uint32_t dest_ip, uint8_t protocol, const void *payload, size_t len);

#endif /* NYOTA_NET_IPV4_H */
