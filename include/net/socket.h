/* =============================================================================
 * Nyota OS — Socket Subsystem Interface
 * BSD-compatible sockets, protocol state management, ring buffers, and wait queues.
 * =========================================================================== */

#ifndef NYOTA_NET_SOCKET_H
#define NYOTA_NET_SOCKET_H

#include "types.h"
#include "net/netdev.h"
#include "net/tcp.h"
#include "process.h"

#define AF_UNSPEC           0
#define AF_UNIX             1
#define AF_LOCAL            1
#define AF_INET             2

#define SOCK_STREAM         1
#define SOCK_DGRAM          2
#define SOCK_RAW            3

#define IPPROTO_IP          0
#define IPPROTO_ICMP        1
#define IPPROTO_TCP         6
#define IPPROTO_UDP         17

#define SHUT_RD             0
#define SHUT_WR             1
#define SHUT_RDWR           2

#define SOCKET_BUFFER_SIZE  8192
#define SOCKET_BACKLOG_MAX  16
#define SOCKET_TABLE_MAX    64

/* BSD Socket Address Structures */
struct in_addr {
    uint32_t s_addr;
};

struct sockaddr {
    uint16_t sa_family;
    char     sa_data[14];
};

struct sockaddr_un {
    uint16_t sun_family;
    char     sun_path[108];
};

struct sockaddr_in {
    uint16_t       sin_family;
    uint16_t       sin_port;
    struct in_addr sin_addr;
    char           sin_zero[8];
};

typedef struct socket socket_t;

/* Socket Structure */
struct socket {
    int          id;
    int          domain;
    int          type;
    int          protocol;
    tcp_state_t  state;

    uint32_t     local_ip;
    uint16_t     local_port;
    uint32_t     remote_ip;
    uint16_t     remote_port;

    /* Unix Domain Socket Support (Phase 9) */
    char         sun_path[108];
    struct socket *peer;

    /* TCP Sequence & Window Management */
    uint32_t     local_seq;
    uint32_t     remote_seq;
    uint32_t     ack_sent;
    uint16_t     remote_window;
    uint64_t     retransmit_timer;

    /* Circular Receive Buffer */
    uint8_t      rx_buf[SOCKET_BUFFER_SIZE];
    size_t       rx_head;
    size_t       rx_tail;
    size_t       rx_count;

    /* Listening Connection Backlog */
    socket_t    *backlog[SOCKET_BACKLOG_MAX];
    size_t       backlog_count;
    size_t       backlog_limit;

    /* Process Sleeping / Wait Queue */
    process_t   *waiters;

    uint32_t     ref_count;
    bool         is_bound;
    bool         in_use;
};

/* Socket Subsystem APIs */
void      socket_init(void);
socket_t *socket_create(int domain, int type, int protocol);
int       socket_bind(socket_t *sock, const struct sockaddr_in *addr);
int       socket_listen(socket_t *sock, int backlog);
socket_t *socket_accept(socket_t *sock, struct sockaddr_in *addr);
int       socket_connect(socket_t *sock, const struct sockaddr_in *addr);
int64_t   socket_send(socket_t *sock, const void *buf, size_t len, int flags);
int64_t   socket_recv(socket_t *sock, void *buf, size_t len, int flags);
int64_t   socket_sendto(socket_t *sock, const void *buf, size_t len, int flags, const struct sockaddr_in *dest);
int64_t   socket_recvfrom(socket_t *sock, void *buf, size_t len, int flags, struct sockaddr_in *src);
int       socket_close(socket_t *sock);

/* Internal Matching & Demux Helpers */
socket_t *socket_find_tcp_listener(uint16_t port);
socket_t *socket_find_tcp_connection(uint32_t local_ip, uint16_t local_port, uint32_t remote_ip, uint16_t remote_port);
socket_t *socket_find_udp(uint32_t local_ip, uint16_t port);

/* Blocking / Waking Helpers */
void      socket_wait(socket_t *sock);
void      socket_wake(socket_t *sock);
void      socket_dispatch_raw(int protocol, uint32_t src_ip, const void *data, size_t len);

#endif /* NYOTA_NET_SOCKET_H */
