/* =============================================================================
 * Nyota OS — Address Resolution Protocol (ARP) Interface
 * =========================================================================== */

#ifndef NYOTA_NET_ARP_H
#define NYOTA_NET_ARP_H

#include "types.h"
#include "net/netdev.h"

#define ARP_HTYPE_ETHERNET  1
#define ARP_PTYPE_IPV4      0x0800

#define ARP_OPER_REQUEST    1
#define ARP_OPER_REPLY      2

#define ARP_CACHE_MAX       32
#define ARP_TIMEOUT_TICKS   (300 * 100) /* 300 seconds (at 100 Hz) */

typedef struct __attribute__((packed)) {
    uint16_t htype;
    uint16_t ptype;
    uint8_t  hlen;
    uint8_t  plen;
    uint16_t oper;
    uint8_t  sha[ETH_ALEN];
    uint32_t spa;
    uint8_t  tha[ETH_ALEN];
    uint32_t tpa;
} arp_packet_t;

typedef struct {
    uint32_t   ip;
    mac_addr_t mac;
    uint64_t   expires_tick;
    bool       valid;
} arp_entry_t;

void arp_init(void);
int  arp_receive(net_device_t *dev, const void *data, size_t len);
int  arp_lookup(net_device_t *dev, uint32_t target_ip, mac_addr_t mac_out);
int  arp_send_request(net_device_t *dev, uint32_t target_ip);
void arp_cache_update(uint32_t ip, const mac_addr_t mac);
void arp_timer_tick(void);

#endif /* NYOTA_NET_ARP_H */
