/* =============================================================================
 * Nyota OS — IPv4 Routing Table Interface
 * Longest prefix match routing lookup, gateway routing, and interface binding.
 * =========================================================================== */

#ifndef NYOTA_NET_ROUTE_H
#define NYOTA_NET_ROUTE_H

#include "types.h"
#include "net/netdev.h"

#define ROUTE_TABLE_MAX     16

typedef struct {
    uint32_t     dest;
    uint32_t     netmask;
    uint32_t     gateway;
    net_device_t *dev;
    bool         valid;
} route_entry_t;

void route_init(void);
int  route_add(uint32_t dest, uint32_t netmask, uint32_t gateway, net_device_t *dev);
int  route_lookup(uint32_t dest, net_device_t **out_dev, uint32_t *out_next_hop);

#endif /* NYOTA_NET_ROUTE_H */
