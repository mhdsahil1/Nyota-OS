/* =============================================================================
 * Nyota OS — Network Packet Buffer Abstraction (packet_t)
 * Reusable network packet structures with head/tail headroom management.
 * =========================================================================== */

#ifndef NYOTA_NET_PACKET_H
#define NYOTA_NET_PACKET_H

#include "types.h"
#include "net/netdev.h"

#define PACKET_MAX_SIZE     2048
#define PACKET_HEADROOM     128

typedef struct packet {
    uint8_t      *buffer;
    uint8_t      *data;
    size_t        len;
    size_t        capacity;
    net_device_t *dev;
} packet_t;

packet_t *packet_alloc(size_t size);
void      packet_free(packet_t *pkt);

/* Prepend header room at front */
void     *packet_push(packet_t *pkt, size_t len);
/* Discard header from front */
void     *packet_pull(packet_t *pkt, size_t len);
/* Append data at back */
void     *packet_put(packet_t *pkt, size_t len);

#endif /* NYOTA_NET_PACKET_H */
