/* =============================================================================
 * Nyota OS — Internet Control Message Protocol (ICMP) Implementation
 * Automatic Echo Reply response and diagnostic ping utilities.
 * =========================================================================== */

#include "net/icmp.h"
#include "net/ipv4.h"
#include "net/socket.h"
#include "drivers/e1000.h"
#include "timer.h"
#include "memory.h"
#include "heap.h"
#include "serial.h"

typedef struct {
    uint32_t ip;
    uint16_t id;
    uint16_t seq;
    uint64_t sent_time_ms;
    uint64_t recv_time_ms;
    bool     replied;
    bool     waiting;
} ping_tracker_t;

static ping_tracker_t current_ping;

void icmp_init(void) {
    memset(&current_ping, 0, sizeof(ping_tracker_t));
}

int icmp_receive(net_device_t *dev, uint32_t src_ip, const void *data, size_t len) {
    if (!dev || !data || len < sizeof(icmp_header_t)) {
        return -1;
    }

    /* 1. Verify ICMP Checksum */
    if (net_checksum(data, len) != 0) {
        return -1; /* Corrupted packet */
    }

    const icmp_header_t *icmp = (const icmp_header_t *)data;

    /* 2. Handle Echo Request (Ping from outside) */
    if (icmp->type == ICMP_TYPE_ECHO_REQUEST) {
        uint8_t reply_buf[1024];
        if (len > sizeof(reply_buf)) return -1;

        memcpy(reply_buf, data, len);
        icmp_header_t *reply = (icmp_header_t *)reply_buf;
        reply->type = ICMP_TYPE_ECHO_REPLY;
        reply->code = 0;
        reply->checksum = 0;

        /* Recompute checksum over entire reply packet */
        reply->checksum = net_checksum(reply_buf, len);

        /* Send Echo Reply directly back to source IP */
        return ipv4_send_from(dev, dev->ip_addr.addr, src_ip, IP_PROTO_ICMP, reply_buf, len);
    }

    /* 3. Handle Echo Reply (Response to our ping) */
    if (icmp->type == ICMP_TYPE_ECHO_REPLY) {
        if (current_ping.waiting &&
            current_ping.ip == src_ip &&
            current_ping.id == ntohs(icmp->id) &&
            current_ping.seq == ntohs(icmp->sequence)) {
            current_ping.recv_time_ms = timer_uptime_ms();
            current_ping.replied = true;
            current_ping.waiting = false;
        }
    }

    /* 4. Forward to any SOCK_RAW sockets listening on ICMP */
    socket_dispatch_raw(IP_PROTO_ICMP, src_ip, data, len);

    return 0;
}

int icmp_send_echo_request(uint32_t target_ip, uint16_t id, uint16_t seq, const void *payload, size_t len) {
    size_t total_len = sizeof(icmp_header_t) + len;
    uint8_t buffer[256];
    if (total_len > sizeof(buffer)) return -1;

    icmp_header_t *icmp = (icmp_header_t *)buffer;
    icmp->type = ICMP_TYPE_ECHO_REQUEST;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->id = htons(id);
    icmp->sequence = htons(seq);

    if (payload && len > 0) {
        memcpy(buffer + sizeof(icmp_header_t), payload, len);
    }

    icmp->checksum = net_checksum(buffer, total_len);

    return ipv4_send(target_ip, IP_PROTO_ICMP, buffer, total_len);
}

int icmp_ping(uint32_t target_ip, uint16_t seq, uint32_t timeout_ms, uint32_t *rtt_ms) {
    if (!rtt_ms) return -1;

    uint16_t id = 0x504E; /* "PN" (Ping Nyota) */
    const char ping_data[] = "NyotaOS Ping Payload 0123456789";

    current_ping.ip = target_ip;
    current_ping.id = id;
    current_ping.seq = seq;
    current_ping.sent_time_ms = timer_uptime_ms();
    current_ping.replied = false;
    current_ping.waiting = true;

    if (icmp_send_echo_request(target_ip, id, seq, ping_data, sizeof(ping_data)) != 0) {
        current_ping.waiting = false;
        return -1;
    }

    uint64_t start = timer_uptime_ms();
    while ((timer_uptime_ms() - start) < timeout_ms) {
        e1000_poll_rx();
        if (current_ping.replied) {
            uint64_t elapsed = current_ping.recv_time_ms - current_ping.sent_time_ms;
            *rtt_ms = (uint32_t)(elapsed == 0 ? 1 : elapsed);
            return 0;
        }
    }

    current_ping.waiting = false;
    return -1; /* Timed out */
}
