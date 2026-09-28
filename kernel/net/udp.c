/* =============================================================================
 * Nyota OS — User Datagram Protocol (UDP) Implementation
 * Port demultiplexing, pseudo-header checksumming, and socket buffer queuing.
 * =========================================================================== */

#include "net/udp.h"
#include "net/ipv4.h"
#include "net/socket.h"
#include "memory.h"
#include "heap.h"
#include "serial.h"

void udp_init(void) {
    /* UDP initialization */
}

int udp_receive(net_device_t *dev, uint32_t src_ip, uint32_t dest_ip, const void *data, size_t len) {
    (void)dev;
    if (!data || len < sizeof(udp_header_t)) {
        return -1;
    }

    const udp_header_t *udp = (const udp_header_t *)data;
    uint16_t src_port = ntohs(udp->src_port);
    uint16_t dest_port = ntohs(udp->dest_port);
    uint16_t udp_len = ntohs(udp->length);

    if (udp_len < sizeof(udp_header_t) || udp_len > len) {
        return -1; /* Truncated or corrupted datagram */
    }

    /* Verify UDP Checksum if present */
    if (udp->checksum != 0) {
        udp_pseudo_header_t psh;
        psh.src_ip = src_ip;
        psh.dest_ip = dest_ip;
        psh.zero = 0;
        psh.protocol = IP_PROTO_UDP;
        psh.udp_length = htons(udp_len);

        uint32_t sum = net_checksum_accum(&psh, sizeof(psh), 0);
        sum = net_checksum_accum(data, udp_len, sum);
        if (net_checksum_finish(sum) != 0) {
            return -1; /* Checksum failure */
        }
    }

    /* Demultiplex to bound UDP socket */
    socket_t *sock = socket_find_udp(dest_ip, dest_port);
    if (!sock) {
        return 0; /* No socket listening on this port */
    }

    size_t payload_len = udp_len - sizeof(udp_header_t);
    const uint8_t *payload = (const uint8_t *)data + sizeof(udp_header_t);

    /* Check if socket buffer has enough capacity: 8 bytes metadata + payload */
    if (sock->rx_count + 8 + payload_len > SOCKET_BUFFER_SIZE) {
        return -1; /* Buffer overflow, drop datagram */
    }

    /* Store 8-byte datagram header: src_ip (4), src_port (2), payload_len (2) */
    uint8_t meta[8];
    memcpy(meta, &src_ip, 4);
    uint16_t sp = htons(src_port);
    memcpy(meta + 4, &sp, 2);
    uint16_t pl = htons((uint16_t)payload_len);
    memcpy(meta + 6, &pl, 2);

    for (int i = 0; i < 8; i++) {
        sock->rx_buf[sock->rx_head] = meta[i];
        sock->rx_head = (sock->rx_head + 1) % SOCKET_BUFFER_SIZE;
    }

    for (size_t i = 0; i < payload_len; i++) {
        sock->rx_buf[sock->rx_head] = payload[i];
        sock->rx_head = (sock->rx_head + 1) % SOCKET_BUFFER_SIZE;
    }

    sock->rx_count += (8 + payload_len);

    /* Wake any processes waiting for data on this socket */
    socket_wake(sock);

    return 0;
}

int udp_send(uint32_t src_ip, uint16_t src_port, uint32_t dest_ip, uint16_t dest_port, const void *payload, size_t len) {
    if (!payload && len > 0) return -1;

    size_t total_len = sizeof(udp_header_t) + len;
    if (total_len > 1472) { /* 1500 MTU - 20 IP - 8 UDP */
        return -1; /* Exceeds maximum unfragmented UDP datagram size */
    }

    uint8_t buffer[1500];
    udp_header_t *udp = (udp_header_t *)buffer;
    udp->src_port = htons(src_port);
    udp->dest_port = htons(dest_port);
    udp->length = htons((uint16_t)total_len);
    udp->checksum = 0;

    if (payload && len > 0) {
        memcpy(buffer + sizeof(udp_header_t), payload, len);
    }

    /* Compute UDP Pseudo-Header Checksum */
    udp_pseudo_header_t psh;
    psh.src_ip = src_ip;
    psh.dest_ip = dest_ip;
    psh.zero = 0;
    psh.protocol = IP_PROTO_UDP;
    psh.udp_length = htons((uint16_t)total_len);

    uint32_t sum = net_checksum_accum(&psh, sizeof(psh), 0);
    sum = net_checksum_accum(buffer, total_len, sum);
    udp->checksum = net_checksum_finish(sum);
    if (udp->checksum == 0) {
        udp->checksum = 0xFFFF;
    }

    return ipv4_send(dest_ip, IP_PROTO_UDP, buffer, total_len);
}
