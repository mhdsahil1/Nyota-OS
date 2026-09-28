/* =============================================================================
 * Nyota OS — Transmission Control Protocol (TCP) Interface
 * State machine, segment serialization, pseudo-header checksum, and connection tracking.
 * =========================================================================== */

#ifndef NYOTA_NET_TCP_H
#define NYOTA_NET_TCP_H

#include "types.h"
#include "net/netdev.h"

/* TCP Header Flags */
#define TCP_FLAG_FIN        0x01
#define TCP_FLAG_SYN        0x02
#define TCP_FLAG_RST        0x04
#define TCP_FLAG_PSH        0x08
#define TCP_FLAG_ACK        0x10
#define TCP_FLAG_URG        0x20

/* TCP Connection States */
typedef enum {
    TCP_STATE_CLOSED = 0,
    TCP_STATE_LISTEN,
    TCP_STATE_SYN_SENT,
    TCP_STATE_SYN_RECEIVED,
    TCP_STATE_ESTABLISHED,
    TCP_STATE_FIN_WAIT_1,
    TCP_STATE_FIN_WAIT_2,
    TCP_STATE_CLOSE_WAIT,
    TCP_STATE_CLOSING,
    TCP_STATE_LAST_ACK,
    TCP_STATE_TIME_WAIT
} tcp_state_t;

typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dest_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  data_offset;   /* Data offset (4 bits) + Reserved (4 bits) */
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
} tcp_header_t;

typedef struct __attribute__((packed)) {
    uint32_t src_ip;
    uint32_t dest_ip;
    uint8_t  zero;
    uint8_t  protocol;
    uint16_t tcp_length;
} tcp_pseudo_header_t;

void        tcp_init(void);
int         tcp_receive(net_device_t *dev, uint32_t src_ip, uint32_t dest_ip, const void *data, size_t len);
void        tcp_timer_tick(void);
const char *tcp_state_to_str(tcp_state_t state);

#endif /* NYOTA_NET_TCP_H */
