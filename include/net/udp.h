/* =============================================================================
 * Nyota OS — User Datagram Protocol (UDP) Interface
 * Port multiplexing, pseudo-header checksum, and datagram transmission.
 * =========================================================================== */

#ifndef NYOTA_NET_UDP_H
#define NYOTA_NET_UDP_H

#include "types.h"
#include "net/netdev.h"

typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dest_port;
    uint16_t length;
    uint16_t checksum;
} udp_header_t;

typedef struct __attribute__((packed)) {
    uint32_t src_ip;
    uint32_t dest_ip;
    uint8_t  zero;
    uint8_t  protocol;
    uint16_t udp_length;
} udp_pseudo_header_t;

void udp_init(void);
int  udp_receive(net_device_t *dev, uint32_t src_ip, uint32_t dest_ip, const void *data, size_t len);
int  udp_send(uint32_t src_ip, uint16_t src_port, uint32_t dest_ip, uint16_t dest_port, const void *payload, size_t len);

#endif /* NYOTA_NET_UDP_H */
