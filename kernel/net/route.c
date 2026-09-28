/* =============================================================================
 * Nyota OS — IPv4 Routing Table Implementation
 * =========================================================================== */

#include "net/route.h"
#include "memory.h"
#include "serial.h"

static route_entry_t route_table[ROUTE_TABLE_MAX];

void route_init(void) {
    memset(route_table, 0, sizeof(route_table));

    /* 1. Loopback Route: 127.0.0.0/8 -> lo */
    net_device_t *lo = netdev_get_by_name("lo");
    if (lo) {
        route_add(ip_parse("127.0.0.0"), ip_parse("255.0.0.0"), 0, lo);
    }

    /* 2. Ethernet Subnet and Gateway Routes */
    net_device_t *eth = netdev_get_by_name("eth0");
    if (eth) {
        /* Direct Subnet: 10.0.2.0/24 -> eth0 */
        route_add(eth->ip_addr.addr & eth->netmask.addr, eth->netmask.addr, 0, eth);

        /* Default Gateway: 0.0.0.0/0 -> 10.0.2.2 via eth0 */
        if (eth->gateway.addr != 0) {
            route_add(0, 0, eth->gateway.addr, eth);
        }
    }
}

int route_add(uint32_t dest, uint32_t netmask, uint32_t gateway, net_device_t *dev) {
    if (!dev) return -1;

    /* Check if route already exists */
    for (int i = 0; i < ROUTE_TABLE_MAX; i++) {
        if (route_table[i].valid &&
            route_table[i].dest == dest &&
            route_table[i].netmask == netmask) {
            route_table[i].gateway = gateway;
            route_table[i].dev = dev;
            return 0;
        }
    }

    /* Find empty slot */
    for (int i = 0; i < ROUTE_TABLE_MAX; i++) {
        if (!route_table[i].valid) {
            route_table[i].dest = dest;
            route_table[i].netmask = netmask;
            route_table[i].gateway = gateway;
            route_table[i].dev = dev;
            route_table[i].valid = true;
            return 0;
        }
    }

    return -1; /* Route table full */
}

int route_lookup(uint32_t dest, net_device_t **out_dev, uint32_t *out_next_hop) {
    if (!out_dev || !out_next_hop) return -1;

    int best_match = -1;
    uint32_t best_mask = 0;

    for (int i = 0; i < ROUTE_TABLE_MAX; i++) {
        if (!route_table[i].valid) continue;

        if ((dest & route_table[i].netmask) == (route_table[i].dest & route_table[i].netmask)) {
            /* Longest prefix match */
            if (best_match == -1 || route_table[i].netmask >= best_mask) {
                best_match = i;
                best_mask = route_table[i].netmask;
            }
        }
    }

    if (best_match >= 0) {
        *out_dev = route_table[best_match].dev;
        *out_next_hop = (route_table[best_match].gateway != 0) ?
                         route_table[best_match].gateway : dest;
        return 0;
    }

    /* Fallback: default device if exists */
    net_device_t *def = netdev_get_default();
    if (def) {
        *out_dev = def;
        *out_next_hop = (def->gateway.addr != 0) ? def->gateway.addr : dest;
        return 0;
    }

    return -1;
}
