/* =============================================================================
 * Nyota OS — Transmission Control Protocol (TCP) Implementation
 * RFC 793 finite state machine, 3-way handshake, payload delivery, and connection teardown.
 * =========================================================================== */

#include "net/tcp.h"
#include "net/ipv4.h"
#include "net/socket.h"
#include "memory.h"
#include "heap.h"
#include "timer.h"
#include "serial.h"

void tcp_init(void) {
    /* TCP initialization */
}

const char *tcp_state_to_str(tcp_state_t state) {
    switch (state) {
        case TCP_STATE_CLOSED:       return "CLOSED";
        case TCP_STATE_LISTEN:       return "LISTEN";
        case TCP_STATE_SYN_SENT:     return "SYN_SENT";
        case TCP_STATE_SYN_RECEIVED: return "SYN_RECV";
        case TCP_STATE_ESTABLISHED:  return "ESTABLISHED";
        case TCP_STATE_FIN_WAIT_1:   return "FIN_WAIT_1";
        case TCP_STATE_FIN_WAIT_2:   return "FIN_WAIT_2";
        case TCP_STATE_CLOSE_WAIT:   return "CLOSE_WAIT";
        case TCP_STATE_CLOSING:      return "CLOSING";
        case TCP_STATE_LAST_ACK:     return "LAST_ACK";
        case TCP_STATE_TIME_WAIT:    return "TIME_WAIT";
        default:                     return "UNKNOWN";
    }
}

/* ── TCP Segment Transmission ─────────────────────────────────────────────── */

int tcp_send_packet(uint32_t src_ip, uint16_t src_port,
                    uint32_t dest_ip, uint16_t dest_port,
                    uint32_t seq, uint32_t ack,
                    uint8_t flags, uint16_t window,
                    const void *payload, size_t payload_len) {
    size_t total_len = sizeof(tcp_header_t) + payload_len;
    if (total_len > 1460 + sizeof(tcp_header_t)) {
        return -1;
    }

    uint8_t buffer[1500];
    tcp_header_t *tcp = (tcp_header_t *)buffer;
    tcp->src_port = htons(src_port);
    tcp->dest_port = htons(dest_port);
    tcp->seq = htonl(seq);
    tcp->ack = htonl(ack);
    tcp->data_offset = (uint8_t)((sizeof(tcp_header_t) / 4) << 4); /* 5 32-bit words = 20 bytes */
    tcp->flags = flags;
    tcp->window = htons(window);
    tcp->checksum = 0;
    tcp->urgent_ptr = 0;

    if (payload && payload_len > 0) {
        memcpy(buffer + sizeof(tcp_header_t), payload, payload_len);
    }

    /* Compute Pseudo-Header Checksum */
    tcp_pseudo_header_t psh;
    psh.src_ip = src_ip;
    psh.dest_ip = dest_ip;
    psh.zero = 0;
    psh.protocol = IP_PROTO_TCP;
    psh.tcp_length = htons((uint16_t)total_len);

    uint32_t sum = net_checksum_accum(&psh, sizeof(psh), 0);
    sum = net_checksum_accum(buffer, total_len, sum);
    tcp->checksum = net_checksum_finish(sum);

    return ipv4_send(dest_ip, IP_PROTO_TCP, buffer, total_len);
}

/* ── TCP Reception & State Machine ────────────────────────────────────────── */

int tcp_receive(net_device_t *dev, uint32_t src_ip, uint32_t dest_ip, const void *data, size_t len) {
    (void)dev;
    if (!data || len < sizeof(tcp_header_t)) {
        return -1;
    }

    const tcp_header_t *tcp = (const tcp_header_t *)data;
    uint16_t src_port = ntohs(tcp->src_port);
    uint16_t dest_port = ntohs(tcp->dest_port);
    uint32_t seq = ntohl(tcp->seq);
    uint32_t ack = ntohl(tcp->ack);
    uint8_t flags = tcp->flags;

    /* 1. Verify Pseudo-Header Checksum if present */
    if (tcp->checksum != 0) {
        tcp_pseudo_header_t psh;
        psh.src_ip = src_ip;
        psh.dest_ip = dest_ip;
        psh.zero = 0;
        psh.protocol = IP_PROTO_TCP;
        psh.tcp_length = htons((uint16_t)len);

        uint32_t sum = net_checksum_accum(&psh, sizeof(psh), 0);
        sum = net_checksum_accum(data, len, sum);
        if (net_checksum_finish(sum) != 0) {
            return -1; /* Checksum mismatch */
        }
    }

    size_t header_len = ((tcp->data_offset >> 4) & 0x0F) * 4;
    if (header_len < sizeof(tcp_header_t) || header_len > len) {
        return -1;
    }

    const uint8_t *payload = (const uint8_t *)data + header_len;
    size_t payload_len = len - header_len;

    /* 2. Demultiplex to matching socket */
    socket_t *sock = socket_find_tcp_connection(dest_ip, dest_port, src_ip, src_port);
    socket_t *listener = NULL;

    if (!sock) {
        listener = socket_find_tcp_listener(dest_port);
    }

    if (!sock && !listener) {
        /* No active connection and no listener: send RST if not already RST */
        if (!(flags & TCP_FLAG_RST)) {
            uint32_t rst_seq = (flags & TCP_FLAG_ACK) ? ack : 0;
            uint32_t rst_ack = seq + (uint32_t)payload_len + ((flags & (TCP_FLAG_SYN | TCP_FLAG_FIN)) ? 1 : 0);
            tcp_send_packet(dest_ip, dest_port, src_ip, src_port, rst_seq, rst_ack,
                            TCP_FLAG_RST | TCP_FLAG_ACK, 0, NULL, 0);
        }
        return 0;
    }

    /* 3. Handle incoming SYN on listening socket */
    if (listener && !sock) {
        if (flags & TCP_FLAG_SYN) {
            if (listener->backlog_count >= listener->backlog_limit) {
                return 0; /* Backlog full, ignore SYN */
            }

            /* Create dedicated client socket for this connection */
            socket_t *client = socket_create(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (!client) return -1;

            client->local_ip = dest_ip;
            client->local_port = dest_port;
            client->remote_ip = src_ip;
            client->remote_port = src_port;
            client->remote_seq = seq + 1; /* SYN consumes 1 sequence number */
            client->local_seq = 10000 + (uint32_t)timer_ticks();
            client->state = TCP_STATE_SYN_RECEIVED;
            client->is_bound = true;

            /* Add to listener's backlog */
            listener->backlog[listener->backlog_count++] = client;

            /* Send SYN + ACK */
            tcp_send_packet(client->local_ip, client->local_port,
                            client->remote_ip, client->remote_port,
                            client->local_seq, client->remote_seq,
                            TCP_FLAG_SYN | TCP_FLAG_ACK, 8192, NULL, 0);
            client->local_seq++;
        }
        return 0;
    }

    /* 4. Active Connection State Machine */
    switch (sock->state) {
        case TCP_STATE_SYN_SENT:
            if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
                sock->remote_seq = seq + 1;
                sock->state = TCP_STATE_ESTABLISHED;

                /* Send ACK completing 3-way handshake */
                tcp_send_packet(sock->local_ip, sock->local_port,
                                sock->remote_ip, sock->remote_port,
                                sock->local_seq, sock->remote_seq,
                                TCP_FLAG_ACK, 8192, NULL, 0);

                /* Wake connect() call */
                socket_wake(sock);
            } else if (flags & TCP_FLAG_RST) {
                sock->state = TCP_STATE_CLOSED;
                socket_wake(sock);
            }
            break;

        case TCP_STATE_SYN_RECEIVED:
            if (flags & TCP_FLAG_ACK) {
                sock->state = TCP_STATE_ESTABLISHED;

                /* Wake accept() call on listening socket if present */
                socket_t *parent_listener = socket_find_tcp_listener(sock->local_port);
                if (parent_listener) {
                    socket_wake(parent_listener);
                }

                /* If ACK segment already carries data payload, queue it immediately */
                if (payload_len > 0) {
                    for (size_t i = 0; i < payload_len; i++) {
                        if (sock->rx_count < SOCKET_BUFFER_SIZE) {
                            sock->rx_buf[sock->rx_head] = payload[i];
                            sock->rx_head = (sock->rx_head + 1) % SOCKET_BUFFER_SIZE;
                            sock->rx_count++;
                        }
                    }

                    sock->remote_seq += (uint32_t)payload_len;

                    /* Send immediate ACK */
                    tcp_send_packet(sock->local_ip, sock->local_port,
                                    sock->remote_ip, sock->remote_port,
                                    sock->local_seq, sock->remote_seq,
                                    TCP_FLAG_ACK, 8192, NULL, 0);

                    socket_wake(sock);
                }
            }
            break;

        case TCP_STATE_ESTABLISHED:
            if (flags & TCP_FLAG_RST) {
                sock->state = TCP_STATE_CLOSED;
                socket_wake(sock);
                return 0;
            }

            /* Deliver inbound payload bytes */
            if (payload_len > 0) {
                for (size_t i = 0; i < payload_len; i++) {
                    if (sock->rx_count < SOCKET_BUFFER_SIZE) {
                        sock->rx_buf[sock->rx_head] = payload[i];
                        sock->rx_head = (sock->rx_head + 1) % SOCKET_BUFFER_SIZE;
                        sock->rx_count++;
                    }
                }

                sock->remote_seq += (uint32_t)payload_len;

                /* Send immediate ACK */
                tcp_send_packet(sock->local_ip, sock->local_port,
                                sock->remote_ip, sock->remote_port,
                                sock->local_seq, sock->remote_seq,
                                TCP_FLAG_ACK, 8192, NULL, 0);

                socket_wake(sock);
            }

            /* Remote host initiated connection teardown (FIN) */
            if (flags & TCP_FLAG_FIN) {
                sock->remote_seq++;
                sock->state = TCP_STATE_CLOSE_WAIT;

                /* Acknowledge FIN */
                tcp_send_packet(sock->local_ip, sock->local_port,
                                sock->remote_ip, sock->remote_port,
                                sock->local_seq, sock->remote_seq,
                                TCP_FLAG_ACK, 8192, NULL, 0);

                socket_wake(sock);
            }
            break;

        case TCP_STATE_FIN_WAIT_1:
            if (flags & TCP_FLAG_ACK) {
                sock->state = TCP_STATE_FIN_WAIT_2;
            }
            if (flags & TCP_FLAG_FIN) {
                sock->remote_seq++;
                tcp_send_packet(sock->local_ip, sock->local_port,
                                sock->remote_ip, sock->remote_port,
                                sock->local_seq, sock->remote_seq,
                                TCP_FLAG_ACK, 8192, NULL, 0);
                sock->state = TCP_STATE_TIME_WAIT;
                socket_wake(sock);
            }
            break;

        case TCP_STATE_FIN_WAIT_2:
            if (flags & TCP_FLAG_FIN) {
                sock->remote_seq++;
                tcp_send_packet(sock->local_ip, sock->local_port,
                                sock->remote_ip, sock->remote_port,
                                sock->local_seq, sock->remote_seq,
                                TCP_FLAG_ACK, 8192, NULL, 0);
                sock->state = TCP_STATE_CLOSED;
                socket_wake(sock);
            }
            break;

        case TCP_STATE_LAST_ACK:
            if (flags & TCP_FLAG_ACK) {
                sock->state = TCP_STATE_CLOSED;
                socket_wake(sock);
            }
            break;

        default:
            break;
    }

    return 0;
}

void tcp_timer_tick(void) {
    /* TCP Retransmission and state timeout handler */
}
