/* =============================================================================
 * Nyota OS — Internet Control Message Protocol (ICMP) Interface
 * Echo Request and Echo Reply handling for diagnostic ping utilities.
 * =========================================================================== */

#ifndef NYOTA_NET_ICMP_H
#define NYOTA_NET_ICMP_H

#include "types.h"
#include "net/netdev.h"

#define ICMP_TYPE_ECHO_REPLY    0
#define ICMP_TYPE_ECHO_REQUEST  8

typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
} icmp_header_t;

void icmp_init(void);
int  icmp_receive(net_device_t *dev, uint32_t src_ip, const void *data, size_t len);
int  icmp_send_echo_request(uint32_t target_ip, uint16_t id, uint16_t seq, const void *payload, size_t len);
int  icmp_ping(uint32_t target_ip, uint16_t seq, uint32_t timeout_ms, uint32_t *rtt_ms);

#endif /* NYOTA_NET_ICMP_H */
