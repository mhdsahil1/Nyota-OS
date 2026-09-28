/* =============================================================================
 * Nyota OS — Ethernet II Framing & Disptach Layer
 * =========================================================================== */

#ifndef NYOTA_NET_ETHERNET_H
#define NYOTA_NET_ETHERNET_H

#include "types.h"
#include "net/netdev.h"

#define ETHERTYPE_IPV4      0x0800
#define ETHERTYPE_ARP       0x0806

#define ETH_MIN_LEN         60
#define ETH_HLEN            14

typedef struct __attribute__((packed)) {
    uint8_t  dest[ETH_ALEN];
    uint8_t  src[ETH_ALEN];
    uint16_t ethertype;
} ethernet_header_t;

/* Global broadcast MAC: FF:FF:FF:FF:FF:FF */
extern const mac_addr_t ETH_BROADCAST_MAC;

bool ethernet_mac_equal(const mac_addr_t a, const mac_addr_t b);
bool ethernet_mac_is_broadcast(const mac_addr_t mac);

int  ethernet_receive(net_device_t *dev, const void *data, size_t len);
int  ethernet_send(net_device_t *dev, const mac_addr_t dest, uint16_t ethertype, const void *payload, size_t len);

#endif /* NYOTA_NET_ETHERNET_H */
