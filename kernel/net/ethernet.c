/* =============================================================================
 * Nyota OS — Ethernet II Framing & Dispatch Implementation
 * =========================================================================== */

#include "net/ethernet.h"
#include "net/arp.h"
#include "net/ipv4.h"
#include "net/ipv6.h"
#include "memory.h"
#include "heap.h"
#include "serial.h"

const mac_addr_t ETH_BROADCAST_MAC = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

bool ethernet_mac_equal(const mac_addr_t a, const mac_addr_t b) {
    for (int i = 0; i < ETH_ALEN; i++) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

bool ethernet_mac_is_broadcast(const mac_addr_t mac) {
    return ethernet_mac_equal(mac, ETH_BROADCAST_MAC);
}

bool ethernet_mac_is_multicast(const mac_addr_t mac) {
    /* IPv6 Multicast (33:33:xx:xx:xx:xx) or IPv4 Multicast (01:00:5E:xx:xx:xx) */
    if (mac[0] == 0x33 && mac[1] == 0x33) return true;
    if (mac[0] == 0x01 && mac[1] == 0x00 && mac[2] == 0x5E) return true;
    return false;
}

int ethernet_receive(net_device_t *dev, const void *data, size_t len) {
    if (!dev || !data || len < sizeof(ethernet_header_t)) {
        return -1;
    }

    const ethernet_header_t *eth = (const ethernet_header_t *)data;

    /* Validate destination MAC: accept if broadcast, multicast, or unicast directed to this adapter */
    if (!ethernet_mac_is_broadcast(eth->dest) &&
        !ethernet_mac_is_multicast(eth->dest) &&
        !ethernet_mac_equal(eth->dest, dev->mac)) {
        /* Not directed to us */
        return 0;
    }

    const uint8_t *payload = (const uint8_t *)data + sizeof(ethernet_header_t);
    size_t payload_len = len - sizeof(ethernet_header_t);
    uint16_t proto = ntohs(eth->ethertype);

    switch (proto) {
        case ETHERTYPE_ARP:
            return arp_receive(dev, payload, payload_len);

        case ETHERTYPE_IPV4:
            return ipv4_receive(dev, payload, payload_len);

        case ETHERTYPE_IPV6:
            return ipv6_receive(dev, payload, payload_len);

        default:
            /* Unsupported EtherType */
            return 0;
    }
}

int ethernet_send(net_device_t *dev, const mac_addr_t dest, uint16_t ethertype, const void *payload, size_t len) {
    if (!dev || !dev->send || !payload) {
        return -1;
    }

    size_t frame_len = sizeof(ethernet_header_t) + len;
    size_t alloc_len = (frame_len < ETH_MIN_LEN) ? ETH_MIN_LEN : frame_len;

    uint8_t buffer[ETH_FRAME_LEN];
    uint8_t *send_buf = buffer;
    bool dyn_alloc = false;

    if (alloc_len > sizeof(buffer)) {
        send_buf = (uint8_t *)kmalloc(alloc_len);
        if (!send_buf) return -1;
        dyn_alloc = true;
    }

    memset(send_buf, 0, alloc_len);

    ethernet_header_t *eth = (ethernet_header_t *)send_buf;
    memcpy(eth->dest, dest, ETH_ALEN);
    memcpy(eth->src, dev->mac, ETH_ALEN);
    eth->ethertype = htons(ethertype);

    memcpy(send_buf + sizeof(ethernet_header_t), payload, len);

    int ret = dev->send(dev, send_buf, alloc_len);

    if (dyn_alloc) {
        kfree(send_buf);
    }

    if (ret == 0) {
        dev->tx_packets++;
        dev->tx_bytes += alloc_len;
    } else {
        dev->tx_errors++;
    }

    return ret;
}
