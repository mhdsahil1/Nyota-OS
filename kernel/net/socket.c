/* =============================================================================
 * Nyota OS — Socket Subsystem Implementation
 * Socket lifecycle, BSD socket calls, circular buffering, and scheduler integration.
 * =========================================================================== */

#include "net/socket.h"
#include "net/tcp.h"
#include "net/udp.h"
#include "net/ipv4.h"
#include "net/icmp.h"
#include "drivers/e1000.h"
#include "memory.h"
#include "heap.h"
#include "timer.h"
#include "process.h"
#include "scheduler.h"
#include "serial.h"
#include "fs/vfs.h"

static socket_t socket_table[SOCKET_TABLE_MAX];
static uint16_t ephemeral_port_counter = 49152;

extern int tcp_send_packet(uint32_t src_ip, uint16_t src_port,
                           uint32_t dest_ip, uint16_t dest_port,
                           uint32_t seq, uint32_t ack,
                           uint8_t flags, uint16_t window,
                           const void *payload, size_t payload_len);

void socket_init(void) {
    memset(socket_table, 0, sizeof(socket_table));
    ephemeral_port_counter = 49152;
}

/* ── Socket Wait Queue Operations ─────────────────────────────────────────── */

static void socket_remove_waiter(socket_t *sock, process_t *proc) {
    if (!sock || !proc) return;
    process_t **curr = &sock->waiters;
    while (*curr) {
        if (*curr == proc) {
            *curr = proc->next;
            proc->next = NULL;
            break;
        }
        curr = &((*curr)->next);
    }
}

void socket_wait(socket_t *sock) {
    if (!sock) return;

    process_t *proc = process_get_current();
    if (!proc || proc == scheduler_get_idle()) return;

    /* Check if already in wait queue */
    process_t *chk = sock->waiters;
    while (chk) {
        if (chk == proc) break;
        chk = chk->next;
    }
    if (!chk) {
        proc->next = sock->waiters;
        sock->waiters = proc;
    }

    proc->state = PROCESS_SLEEPING;
    scheduler_yield();

    while (proc->state == PROCESS_SLEEPING) {
        __asm__ volatile ("sti; hlt");
        e1000_poll_rx();
    }

    proc->state = PROCESS_RUNNING;
    socket_remove_waiter(sock, proc);
}

void socket_wake(socket_t *sock) {
    if (!sock || !sock->waiters) return;

    process_t *curr = sock->waiters;
    sock->waiters = NULL;

    while (curr) {
        process_t *nxt = curr->next;
        curr->next = NULL;
        curr->prev = NULL;
        curr->state = PROCESS_READY;
        scheduler_add(curr);
        curr = nxt;
    }

    scheduler_request_reschedule();
}

void socket_dispatch_raw(int protocol, uint32_t src_ip, const void *data, size_t len) {
    for (int i = 0; i < SOCKET_TABLE_MAX; i++) {
        socket_t *sock = &socket_table[i];
        if (sock->in_use && sock->type == SOCK_RAW && sock->protocol == protocol) {
            if (sock->rx_count + 8 + len > SOCKET_BUFFER_SIZE) continue;

            uint8_t meta[8];
            memcpy(meta, &src_ip, 4);
            uint16_t sp = 0;
            memcpy(meta + 4, &sp, 2);
            uint16_t pl = htons((uint16_t)len);
            memcpy(meta + 6, &pl, 2);

            for (int m = 0; m < 8; m++) {
                sock->rx_buf[sock->rx_head] = meta[m];
                sock->rx_head = (sock->rx_head + 1) % SOCKET_BUFFER_SIZE;
            }

            const uint8_t *p = (const uint8_t *)data;
            for (size_t d = 0; d < len; d++) {
                sock->rx_buf[sock->rx_head] = p[d];
                sock->rx_head = (sock->rx_head + 1) % SOCKET_BUFFER_SIZE;
            }

            sock->rx_count += (8 + len);
            socket_wake(sock);
        }
    }
}

/* ── Lookup Helpers ───────────────────────────────────────────────────────── */

socket_t *socket_find_tcp_listener(uint16_t port) {
    for (int i = 0; i < SOCKET_TABLE_MAX; i++) {
        if (socket_table[i].in_use &&
            socket_table[i].type == SOCK_STREAM &&
            socket_table[i].state == TCP_STATE_LISTEN &&
            socket_table[i].local_port == port) {
            return &socket_table[i];
        }
    }
    return NULL;
}

socket_t *socket_find_tcp_connection(uint32_t local_ip, uint16_t local_port,
                                     uint32_t remote_ip, uint16_t remote_port) {
    for (int i = 0; i < SOCKET_TABLE_MAX; i++) {
        if (socket_table[i].in_use &&
            socket_table[i].type == SOCK_STREAM &&
            socket_table[i].state != TCP_STATE_LISTEN &&
            socket_table[i].local_port == local_port &&
            socket_table[i].remote_port == remote_port &&
            (socket_table[i].local_ip == local_ip || socket_table[i].local_ip == 0) &&
            socket_table[i].remote_ip == remote_ip) {
            return &socket_table[i];
        }
    }
    return NULL;
}

socket_t *socket_find_udp(uint32_t local_ip, uint16_t port) {
    for (int i = 0; i < SOCKET_TABLE_MAX; i++) {
        if (socket_table[i].in_use &&
            socket_table[i].type == SOCK_DGRAM &&
            socket_table[i].local_port == port &&
            (socket_table[i].local_ip == local_ip || socket_table[i].local_ip == 0)) {
            return &socket_table[i];
        }
    }
    return NULL;
}

/* ── Lifecycle & Socket Operations ────────────────────────────────────────── */

socket_t *socket_create(int domain, int type, int protocol) {
    if (domain != AF_INET && domain != AF_UNSPEC && domain != AF_UNIX) {
        return NULL;
    }

    /* Assign default protocol if unspecified */
    if (protocol == 0) {
        if (type == SOCK_STREAM) protocol = IPPROTO_TCP;
        else if (type == SOCK_DGRAM) protocol = IPPROTO_UDP;
        else if (type == SOCK_RAW) protocol = IPPROTO_ICMP;
    }

    for (int i = 0; i < SOCKET_TABLE_MAX; i++) {
        if (!socket_table[i].in_use) {
            socket_t *s = &socket_table[i];
            memset(s, 0, sizeof(socket_t));

            s->id = i;
            s->domain = domain;
            s->type = type;
            s->protocol = protocol;
            s->state = TCP_STATE_CLOSED;
            s->backlog_limit = 5;
            s->in_use = true;
            s->ref_count = 1;

            return s;
        }
    }
    return NULL;
}

int socket_bind(socket_t *sock, const struct sockaddr_in *addr) {
    if (!sock || !addr) return -1;

    if (sock->domain == AF_UNIX) {
        const struct sockaddr_un *un = (const struct sockaddr_un *)addr;
        size_t plen = strlen(un->sun_path);
        if (plen >= sizeof(sock->sun_path)) plen = sizeof(sock->sun_path) - 1;
        memcpy(sock->sun_path, un->sun_path, plen);
        sock->sun_path[plen] = '\0';
        sock->is_bound = true;

        /* Create filesystem entry so socket shows in /run and stat() works */
        vfs_create_entry(sock->sun_path, 0140666);
        return 0;
    }

    sock->local_ip = addr->sin_addr.s_addr;
    sock->local_port = ntohs(addr->sin_port);
    sock->is_bound = true;

    return 0;
}

int socket_listen(socket_t *sock, int backlog) {
    if (!sock || sock->type != SOCK_STREAM) return -1;

    sock->backlog_limit = (backlog > SOCKET_BACKLOG_MAX) ? SOCKET_BACKLOG_MAX :
                          (backlog < 1 ? 1 : backlog);
    sock->state = TCP_STATE_LISTEN;

    return 0;
}

socket_t *socket_accept(socket_t *sock, struct sockaddr_in *addr) {
    if (!sock || sock->state != TCP_STATE_LISTEN) return NULL;

    /* Wait until a connection in backlog reaches ESTABLISHED state */
    while (1) {
        for (size_t i = 0; i < sock->backlog_count; i++) {
            socket_t *client = sock->backlog[i];
            if (client && client->state == TCP_STATE_ESTABLISHED) {
                /* Remove from backlog by shifting */
                for (size_t j = i; j < sock->backlog_count - 1; j++) {
                    sock->backlog[j] = sock->backlog[j + 1];
                }
                sock->backlog[--sock->backlog_count] = NULL;

                if (addr) {
                    if (sock->domain == AF_UNIX) {
                        addr->sin_family = AF_UNIX;
                    } else {
                        addr->sin_family = AF_INET;
                        addr->sin_port = htons(client->remote_port);
                        addr->sin_addr.s_addr = client->remote_ip;
                    }
                }

                process_t *curr = process_get_current();
                if (curr) {
                    curr->state = PROCESS_RUNNING;
                    socket_remove_waiter(sock, curr);
                }

                return client;
            }
        }

        process_t *curr = process_get_current();
        if (curr && curr->pending_signals) {
            socket_remove_waiter(sock, curr);
            curr->state = PROCESS_RUNNING;
            return NULL;
        }

        e1000_poll_rx();
        socket_wait(sock);
    }
}

int socket_connect(socket_t *sock, const struct sockaddr_in *addr) {
    if (!sock || !addr) return -1;

    if (sock->domain == AF_UNIX) {
        const struct sockaddr_un *un = (const struct sockaddr_un *)addr;
        socket_t *listener = NULL;
        for (int i = 0; i < SOCKET_TABLE_MAX; i++) {
            if (socket_table[i].in_use && socket_table[i].domain == AF_UNIX &&
                socket_table[i].state == TCP_STATE_LISTEN &&
                strcmp(socket_table[i].sun_path, un->sun_path) == 0) {
                listener = &socket_table[i];
                break;
            }
        }
        if (!listener || listener->backlog_count >= listener->backlog_limit) {
            return -1;
        }

        socket_t *server_conn = NULL;
        for (int i = 0; i < SOCKET_TABLE_MAX; i++) {
            if (!socket_table[i].in_use) {
                server_conn = &socket_table[i];
                memset(server_conn, 0, sizeof(socket_t));
                server_conn->id = i;
                server_conn->domain = AF_UNIX;
                server_conn->type = SOCK_STREAM;
                server_conn->state = TCP_STATE_ESTABLISHED;
                server_conn->in_use = true;
                server_conn->ref_count = 1;
                break;
            }
        }
        if (!server_conn) return -1;

        server_conn->peer = sock;
        sock->peer = server_conn;
        sock->state = TCP_STATE_ESTABLISHED;

        listener->backlog[listener->backlog_count++] = server_conn;
        socket_wake(listener);
        return 0;
    }

    sock->remote_ip = addr->sin_addr.s_addr;
    sock->remote_port = ntohs(addr->sin_port);

    /* Auto-assign ephemeral local port if unbound */
    if (sock->local_port == 0) {
        sock->local_port = ephemeral_port_counter++;
        if (ephemeral_port_counter >= 65000) ephemeral_port_counter = 49152;
    }

    /* Auto-assign local interface IP */
    if (sock->local_ip == 0) {
        net_device_t *def = netdev_get_default();
        if ((sock->remote_ip & 0xFF) == 127) {
            net_device_t *lo = netdev_get_by_name("lo");
            sock->local_ip = lo ? lo->ip_addr.addr : ip_parse("127.0.0.1");
        } else {
            sock->local_ip = def ? def->ip_addr.addr : ip_parse("10.0.2.15");
        }
    }

    if (sock->type == SOCK_STREAM) {
        sock->local_seq = 1000 + (uint32_t)timer_ticks();
        sock->state = TCP_STATE_SYN_SENT;
        sock->is_bound = true;

        /* Send initial SYN */
        tcp_send_packet(sock->local_ip, sock->local_port,
                        sock->remote_ip, sock->remote_port,
                        sock->local_seq, 0,
                        TCP_FLAG_SYN, 8192, NULL, 0);
        sock->local_seq++;

        /* Wait for 3-way handshake completion */
        uint64_t start_ms = timer_uptime_ms();
        while (sock->state == TCP_STATE_SYN_SENT) {
            e1000_poll_rx();
            if (timer_uptime_ms() - start_ms > 4000) {
                sock->state = TCP_STATE_CLOSED;
                return -1; /* Connection timeout */
            }
            socket_wait(sock);
        }

        process_t *curr = process_get_current();
        if (curr) {
            curr->state = PROCESS_RUNNING;
            socket_remove_waiter(sock, curr);
        }

        return (sock->state == TCP_STATE_ESTABLISHED) ? 0 : -1;
    }

    return 0;
}

int64_t socket_send(socket_t *sock, const void *buf, size_t len, int flags) {
    (void)flags;
    if (!sock || (!buf && len > 0)) return -1;

    if (sock->domain == AF_UNIX) {
        if (!sock->peer || sock->peer->state != TCP_STATE_ESTABLISHED) {
            return -1;
        }
        socket_t *peer = sock->peer;
        size_t written = 0;
        const uint8_t *src = (const uint8_t *)buf;
        while (written < len && peer->rx_count < SOCKET_BUFFER_SIZE) {
            peer->rx_buf[peer->rx_head] = src[written++];
            peer->rx_head = (peer->rx_head + 1) % SOCKET_BUFFER_SIZE;
            peer->rx_count++;
        }
        socket_wake(peer);
        return (int64_t)written;
    }

    if (sock->type == SOCK_STREAM) {
        if (sock->state != TCP_STATE_ESTABLISHED) {
            return -1;
        }

        size_t total_sent = 0;
        const uint8_t *p = (const uint8_t *)buf;

        while (total_sent < len) {
            size_t chunk = len - total_sent;
            if (chunk > 1460) chunk = 1460;

            int res = tcp_send_packet(sock->local_ip, sock->local_port,
                                      sock->remote_ip, sock->remote_port,
                                      sock->local_seq, sock->remote_seq,
                                      TCP_FLAG_PSH | TCP_FLAG_ACK, 8192,
                                      p + total_sent, chunk);
            if (res != 0) return -1;

            sock->local_seq += (uint32_t)chunk;
            total_sent += chunk;
        }

        return (int64_t)total_sent;
    } else if (sock->type == SOCK_DGRAM) {
        int res = udp_send(sock->local_ip, sock->local_port,
                           sock->remote_ip, sock->remote_port, buf, len);
        return (res == 0) ? (int64_t)len : -1;
    }

    return -1;
}

int64_t socket_recv(socket_t *sock, void *buf, size_t len, int flags) {
    (void)flags;
    if (!sock || !buf || len == 0) return -1;

    if (sock->domain == AF_UNIX) {
        while (sock->rx_count == 0) {
            if (!sock->peer || sock->peer->state != TCP_STATE_ESTABLISHED) {
                return 0; /* Peer closed, EOF */
            }
            socket_wait(sock);
        }
        size_t to_read = (len < sock->rx_count) ? len : sock->rx_count;
        uint8_t *dst = (uint8_t *)buf;
        for (size_t i = 0; i < to_read; i++) {
            dst[i] = sock->rx_buf[sock->rx_tail];
            sock->rx_tail = (sock->rx_tail + 1) % SOCKET_BUFFER_SIZE;
        }
        sock->rx_count -= to_read;
        return (int64_t)to_read;
    }

    if (sock->type == SOCK_STREAM) {
        /* Wait while no data is available in buffer */
        while (sock->rx_count == 0) {
            if (sock->state != TCP_STATE_ESTABLISHED && sock->state != TCP_STATE_CLOSE_WAIT) {
                return 0; /* Connection closed */
            }
            if (sock->state == TCP_STATE_CLOSE_WAIT && sock->rx_count == 0) {
                return 0; /* EOF */
            }
            e1000_poll_rx();
            socket_wait(sock);
        }

        size_t to_read = (len < sock->rx_count) ? len : sock->rx_count;
        uint8_t *dst = (uint8_t *)buf;

        for (size_t i = 0; i < to_read; i++) {
            dst[i] = sock->rx_buf[sock->rx_tail];
            sock->rx_tail = (sock->rx_tail + 1) % SOCKET_BUFFER_SIZE;
        }
        sock->rx_count -= to_read;

        process_t *curr = process_get_current();
        if (curr) {
            curr->state = PROCESS_RUNNING;
            socket_remove_waiter(sock, curr);
        }

        return (int64_t)to_read;
    } else if (sock->type == SOCK_DGRAM) {
        /* Extract queued UDP datagram */
        while (sock->rx_count < 8) {
            e1000_poll_rx();
            socket_wait(sock);
        }

        uint8_t meta[8];
        for (int i = 0; i < 8; i++) {
            meta[i] = sock->rx_buf[sock->rx_tail];
            sock->rx_tail = (sock->rx_tail + 1) % SOCKET_BUFFER_SIZE;
        }

        uint16_t payload_len = (meta[6] << 8) | meta[7];
        size_t to_copy = (len < payload_len) ? len : payload_len;
        uint8_t *dst = (uint8_t *)buf;

        for (size_t i = 0; i < payload_len; i++) {
            uint8_t byte = sock->rx_buf[sock->rx_tail];
            sock->rx_tail = (sock->rx_tail + 1) % SOCKET_BUFFER_SIZE;
            if (i < to_copy) {
                dst[i] = byte;
            }
        }

        sock->rx_count -= (8 + payload_len);
        return (int64_t)to_copy;
    }

    return -1;
}

int64_t socket_sendto(socket_t *sock, const void *buf, size_t len, int flags, const struct sockaddr_in *dest) {
    (void)flags;
    if (!sock || !dest) return -1;

    if (sock->local_port == 0) {
        sock->local_port = ephemeral_port_counter++;
        if (ephemeral_port_counter >= 65000) ephemeral_port_counter = 49152;
    }

    if (sock->local_ip == 0) {
        net_device_t *def = netdev_get_default();
        sock->local_ip = def ? def->ip_addr.addr : ip_parse("10.0.2.15");
    }

    if (sock->type == SOCK_DGRAM) {
        int res = udp_send(sock->local_ip, sock->local_port,
                           dest->sin_addr.s_addr, ntohs(dest->sin_port),
                           buf, len);
        return (res == 0) ? (int64_t)len : -1;
    }

    if (sock->type == SOCK_RAW) {
        int res = ipv4_send(dest->sin_addr.s_addr, (uint8_t)sock->protocol, buf, len);
        return (res == 0) ? (int64_t)len : -1;
    }

    return -1;
}

int64_t socket_recvfrom(socket_t *sock, void *buf, size_t len, int flags, struct sockaddr_in *src) {
    (void)flags;
    if (!sock || !buf || len == 0) return -1;

    if (sock->type == SOCK_DGRAM || sock->type == SOCK_RAW) {
        while (sock->rx_count < 8) {
            e1000_poll_rx();
            socket_wait(sock);
        }

        uint8_t meta[8];
        for (int i = 0; i < 8; i++) {
            meta[i] = sock->rx_buf[sock->rx_tail];
            sock->rx_tail = (sock->rx_tail + 1) % SOCKET_BUFFER_SIZE;
        }

        uint32_t sender_ip;
        memcpy(&sender_ip, meta, 4);
        uint16_t sender_port = (meta[4] << 8) | meta[5];
        uint16_t payload_len = (meta[6] << 8) | meta[7];

        if (src) {
            src->sin_family = AF_INET;
            src->sin_addr.s_addr = sender_ip;
            src->sin_port = htons(sender_port);
        }

        size_t to_copy = (len < payload_len) ? len : payload_len;
        uint8_t *dst = (uint8_t *)buf;

        for (size_t i = 0; i < payload_len; i++) {
            uint8_t byte = sock->rx_buf[sock->rx_tail];
            sock->rx_tail = (sock->rx_tail + 1) % SOCKET_BUFFER_SIZE;
            if (i < to_copy) {
                dst[i] = byte;
            }
        }

        sock->rx_count -= (8 + payload_len);
        return (int64_t)to_copy;
    }

    return socket_recv(sock, buf, len, flags);
}

int socket_close(socket_t *sock) {
    if (!sock || !sock->in_use) return -1;

    if (sock->ref_count > 1) {
        sock->ref_count--;
        return 0;
    }

    if (sock->domain == AF_UNIX) {
        if (sock->peer) {
            sock->peer->peer = NULL;
            socket_wake(sock->peer);
            sock->peer = NULL;
        }
        socket_wake(sock);
        sock->in_use = false;
        sock->state = TCP_STATE_CLOSED;
        return 0;
    }

    if (sock->type == SOCK_STREAM) {
        if (sock->state == TCP_STATE_ESTABLISHED) {
            tcp_send_packet(sock->local_ip, sock->local_port,
                            sock->remote_ip, sock->remote_port,
                            sock->local_seq, sock->remote_seq,
                            TCP_FLAG_FIN | TCP_FLAG_ACK, 8192, NULL, 0);
            sock->local_seq++;
            sock->state = TCP_STATE_FIN_WAIT_1;
        } else if (sock->state == TCP_STATE_CLOSE_WAIT) {
            tcp_send_packet(sock->local_ip, sock->local_port,
                            sock->remote_ip, sock->remote_port,
                            sock->local_seq, sock->remote_seq,
                            TCP_FLAG_FIN | TCP_FLAG_ACK, 8192, NULL, 0);
            sock->local_seq++;
            sock->state = TCP_STATE_LAST_ACK;
        }
    }

    socket_wake(sock);
    sock->in_use = false;
    sock->state = TCP_STATE_CLOSED;

    return 0;
}
