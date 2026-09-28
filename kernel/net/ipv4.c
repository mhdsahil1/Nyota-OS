/* =============================================================================
 * Nyota OS — Internet Protocol Version 4 (IPv4) Implementation
 * Packet validation, RFC 1071 checksum calculation, routing, and protocol dispatch.
 * =========================================================================== */

#include "net/ipv4.h"
#include "net/ethernet.h"
#include "net/arp.h"
#include "net/route.h"
#include "net/icmp.h"
#include "net/udp.h"
#include "net/tcp.h"
#include "memory.h"
#include "heap.h"
#include "serial.h"

static uint16_t ipv4_packet_id = 1;

/* ── Internet Checksum Calculation (RFC 1071) ──────────────────────────────── */

uint32_t net_checksum_accum(const void *data, size_t len, uint32_t sum) {
    const uint16_t *p = (const uint16_t *)data;

    while (len > 1) {
        sum += *p++;
        len -= 2;
    }

    if (len == 1) {
        sum += *(const uint8_t *)p;
    }

    return sum;
}

uint16_t net_checksum_finish(uint32_t sum) {
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

uint16_t net_checksum(const void *data, size_t len) {
    uint32_t sum = net_checksum_accum(data, len, 0);
    return net_checksum_finish(sum);
}

/* ── Initialization ───────────────────────────────────────────────────────── */

void ipv4_init(void) {
    ipv4_packet_id = 1;
    route_init();
}

/* ── Reception & Protocol Demultiplexing ───────────────────────────────────── */

int ipv4_receive(net_device_t *dev, const void *data, size_t len) {
    if (!dev || !data || len < sizeof(ipv4_header_t)) {
        return -1;
    }

    const ipv4_header_t *ip = (const ipv4_header_t *)data;

    /* 1. Validate IPv4 version (high nibble must be 4) */
    uint8_t version = (ip->version_ihl >> 4) & 0x0F;
    if (version != 4) {
        return -1;
    }

    /* 2. Validate Internet Header Length (IHL in 32-bit words, minimum 5 = 20 bytes) */
    size_t ihl = (ip->version_ihl & 0x0F) * 4;
    if (ihl < sizeof(ipv4_header_t) || ihl > len) {
        return -1;
    }

    /* 3. Validate Header Checksum */
    if (net_checksum(ip, ihl) != 0) {
        return -1;
    }

    /* 4. Validate Total Length */
    size_t total_len = ntohs(ip->total_length);
    if (total_len > len || total_len < ihl) {
        return -1;
    }

    /* 5. Destination address check */
    bool for_us = (ip->dest == dev->ip_addr.addr) ||
                  (ip->dest == 0xFFFFFFFF) ||
                  (ip->dest == 0) ||
                  ((ip->dest & 0xFF) == 127); /* Loopback */

    if (!for_us) {
        return 0; /* Not addressed to this system */
    }

    const uint8_t *payload = (const uint8_t *)data + ihl;
    size_t payload_len = total_len - ihl;

    /* 6. Protocol demultiplexing */
    switch (ip->protocol) {
        case IP_PROTO_ICMP:
            return icmp_receive(dev, ip->src, payload, payload_len);

        case IP_PROTO_UDP:
            return udp_receive(dev, ip->src, ip->dest, payload, payload_len);

        case IP_PROTO_TCP:
            return tcp_receive(dev, ip->src, ip->dest, payload, payload_len);

        default:
            return 0; /* Unsupported transport protocol */
    }
}

/* ── Transmission & Routing ───────────────────────────────────────────────── */

int ipv4_send_from(net_device_t *dev, uint32_t src_ip, uint32_t dest_ip, uint8_t protocol, const void *payload, size_t len) {
    if (!dev || !payload) return -1;

    size_t total_len = sizeof(ipv4_header_t) + len;
    if (total_len > dev->mtu) {
        return -1; /* Exceeds MTU */
    }

    uint8_t buffer[ETH_FRAME_LEN];
    uint8_t *send_buf = buffer;
    bool dyn_alloc = false;

    if (total_len > sizeof(buffer)) {
        send_buf = (uint8_t *)kmalloc(total_len);
        if (!send_buf) return -1;
        dyn_alloc = true;
    }

    ipv4_header_t *ip = (ipv4_header_t *)send_buf;
    ip->version_ihl = 0x45; /* Version 4, IHL 5 (20 bytes) */
    ip->tos = 0;
    ip->total_length = htons((uint16_t)total_len);
    ip->id = htons(ipv4_packet_id++);
    ip->flags_frag = 0;
    ip->ttl = IPV4_DEFAULT_TTL;
    ip->protocol = protocol;
    ip->checksum = 0;
    ip->src = src_ip;
    ip->dest = dest_ip;

    /* Compute header checksum */
    ip->checksum = net_checksum(ip, sizeof(ipv4_header_t));

    /* Copy transport payload */
    memcpy(send_buf + sizeof(ipv4_header_t), payload, len);

    int ret = 0;

    if (strcmp(dev->name, "lo") == 0) {
        /* Loopback interface bypasses Ethernet and ARP */
        ret = dev->send(dev, send_buf, total_len);
    } else {
        /* Look up next hop route */
        net_device_t *route_dev = NULL;
        uint32_t next_hop = 0;

        if (route_lookup(dest_ip, &route_dev, &next_hop) != 0) {
            next_hop = dest_ip;
        }

        /* Resolve next-hop MAC via ARP */
        mac_addr_t dest_mac;
        if (arp_lookup(dev, next_hop, dest_mac) != 0) {
            if (dyn_alloc) kfree(send_buf);
            return -1; /* Unreachable host / ARP failure */
        }

        ret = ethernet_send(dev, dest_mac, ETHERTYPE_IPV4, send_buf, total_len);
    }

    if (dyn_alloc) {
        kfree(send_buf);
    }

    return ret;
}

int ipv4_send(uint32_t dest_ip, uint8_t protocol, const void *payload, size_t len) {
    net_device_t *dev = NULL;
    uint32_t next_hop = 0;

    if (route_lookup(dest_ip, &dev, &next_hop) != 0 || !dev) {
        dev = netdev_get_default();
    }

    if (!dev) return -1;

    uint32_t src_ip = dev->ip_addr.addr;
    return ipv4_send_from(dev, src_ip, dest_ip, protocol, payload, len);
}
