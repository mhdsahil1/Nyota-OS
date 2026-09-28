/* =============================================================================
 * Nyota OS — Network Packet Buffer Implementation
 * =========================================================================== */

#include "net/packet.h"
#include "heap.h"
#include "memory.h"

packet_t *packet_alloc(size_t size) {
    if (size == 0) size = PACKET_MAX_SIZE;
    size_t total_cap = size + PACKET_HEADROOM;

    packet_t *pkt = (packet_t *)kmalloc(sizeof(packet_t));
    if (!pkt) return NULL;

    pkt->buffer = (uint8_t *)kmalloc(total_cap);
    if (!pkt->buffer) {
        kfree(pkt);
        return NULL;
    }

    pkt->data = pkt->buffer + PACKET_HEADROOM;
    pkt->len = 0;
    pkt->capacity = size;
    pkt->dev = NULL;

    return pkt;
}

void packet_free(packet_t *pkt) {
    if (!pkt) return;
    if (pkt->buffer) {
        kfree(pkt->buffer);
    }
    kfree(pkt);
}

void *packet_push(packet_t *pkt, size_t len) {
    if (!pkt || (pkt->data - len) < pkt->buffer) return NULL;
    pkt->data -= len;
    pkt->len += len;
    return pkt->data;
}

void *packet_pull(packet_t *pkt, size_t len) {
    if (!pkt || len > pkt->len) return NULL;
    void *old = pkt->data;
    pkt->data += len;
    pkt->len -= len;
    return old;
}

void *packet_put(packet_t *pkt, size_t len) {
    if (!pkt || (pkt->len + len) > pkt->capacity) return NULL;
    void *tail = pkt->data + pkt->len;
    pkt->len += len;
    return tail;
}
