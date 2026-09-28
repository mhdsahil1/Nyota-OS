/* =============================================================================
 * Nyota OS — Address Resolution Protocol (ARP) Implementation
 * =========================================================================== */

#include "net/arp.h"
#include "net/ethernet.h"
#include "drivers/e1000.h"
#include "timer.h"
#include "memory.h"
#include "serial.h"

static arp_entry_t arp_cache[ARP_CACHE_MAX];

void arp_init(void) {
    memset(arp_cache, 0, sizeof(arp_cache));
}

void arp_cache_update(uint32_t ip, const mac_addr_t mac) {
    if (ip == 0) return;

    /* 1. Check if entry already exists */
    for (int i = 0; i < ARP_CACHE_MAX; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(arp_cache[i].mac, mac, ETH_ALEN);
            arp_cache[i].expires_tick = timer_ticks() + ARP_TIMEOUT_TICKS;
            return;
        }
    }

    /* 2. Find empty or oldest slot */
    int slot = -1;
    uint64_t oldest_expiry = 0xFFFFFFFFFFFFFFFFULL;

    for (int i = 0; i < ARP_CACHE_MAX; i++) {
        if (!arp_cache[i].valid) {
            slot = i;
            break;
        }
        if (arp_cache[i].expires_tick < oldest_expiry) {
            oldest_expiry = arp_cache[i].expires_tick;
            slot = i;
        }
    }

    if (slot >= 0) {
        arp_cache[slot].ip = ip;
        memcpy(arp_cache[slot].mac, mac, ETH_ALEN);
        arp_cache[slot].expires_tick = timer_ticks() + ARP_TIMEOUT_TICKS;
        arp_cache[slot].valid = true;
    }
}

int arp_send_request(net_device_t *dev, uint32_t target_ip) {
    if (!dev) return -1;

    arp_packet_t req;
    req.htype = htons(ARP_HTYPE_ETHERNET);
    req.ptype = htons(ARP_PTYPE_IPV4);
    req.hlen  = ETH_ALEN;
    req.plen  = 4;
    req.oper  = htons(ARP_OPER_REQUEST);

    memcpy(req.sha, dev->mac, ETH_ALEN);
    req.spa = dev->ip_addr.addr;
    memset(req.tha, 0, ETH_ALEN);
    req.tpa = target_ip;

    return ethernet_send(dev, ETH_BROADCAST_MAC, ETHERTYPE_ARP, &req, sizeof(req));
}

int arp_lookup(net_device_t *dev, uint32_t target_ip, mac_addr_t mac_out) {
    if (!dev || !mac_out) return -1;

    /* Broadcast IP gets Broadcast MAC directly */
    if (target_ip == 0xFFFFFFFF || target_ip == 0) {
        memcpy(mac_out, ETH_BROADCAST_MAC, ETH_ALEN);
        return 0;
    }

    /* 1. Check cache */
    for (int i = 0; i < ARP_CACHE_MAX; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == target_ip) {
            memcpy(mac_out, arp_cache[i].mac, ETH_ALEN);
            return 0;
        }
    }

    /* 2. Send ARP request and wait for reply */
    arp_send_request(dev, target_ip);

    uint64_t start = timer_ticks();
    while (timer_ticks() - start < 15) { /* ~150ms timeout */
        e1000_poll_rx();
        for (int i = 0; i < ARP_CACHE_MAX; i++) {
            if (arp_cache[i].valid && arp_cache[i].ip == target_ip) {
                memcpy(mac_out, arp_cache[i].mac, ETH_ALEN);
                return 0;
            }
        }
    }

    return -1; /* Host unreachable / ARP timeout */
}

int arp_receive(net_device_t *dev, const void *data, size_t len) {
    if (!dev || !data || len < sizeof(arp_packet_t)) {
        return -1;
    }

    const arp_packet_t *arp = (const arp_packet_t *)data;

    if (ntohs(arp->htype) != ARP_HTYPE_ETHERNET ||
        ntohs(arp->ptype) != ARP_PTYPE_IPV4 ||
        arp->hlen != ETH_ALEN || arp->plen != 4) {
        return -1; /* Malformed ARP packet */
    }

    /* Cache sender information */
    arp_cache_update(arp->spa, arp->sha);

    uint16_t oper = ntohs(arp->oper);

    if (oper == ARP_OPER_REQUEST) {
        /* If the target IP matches our adapter IP, answer with ARP Reply */
        if (arp->tpa == dev->ip_addr.addr) {
            arp_packet_t reply;
            reply.htype = htons(ARP_HTYPE_ETHERNET);
            reply.ptype = htons(ARP_PTYPE_IPV4);
            reply.hlen  = ETH_ALEN;
            reply.plen  = 4;
            reply.oper  = htons(ARP_OPER_REPLY);

            memcpy(reply.sha, dev->mac, ETH_ALEN);
            reply.spa = dev->ip_addr.addr;
            memcpy(reply.tha, arp->sha, ETH_ALEN);
            reply.tpa = arp->spa;

            ethernet_send(dev, arp->sha, ETHERTYPE_ARP, &reply, sizeof(reply));
        }
    }

    return 0;
}

void arp_timer_tick(void) {
    uint64_t now = timer_ticks();
    for (int i = 0; i < ARP_CACHE_MAX; i++) {
        if (arp_cache[i].valid && now >= arp_cache[i].expires_tick) {
            arp_cache[i].valid = false;
        }
    }
}
