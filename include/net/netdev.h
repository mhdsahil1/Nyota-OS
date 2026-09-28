/* =============================================================================
 * Nyota OS — Network Device Abstraction (net_device_t)
 * Common interface for physical (E1000) and virtual (loopback) network adapters.
 * =========================================================================== */

#ifndef NYOTA_NET_NETDEV_H
#define NYOTA_NET_NETDEV_H

#include "types.h"

#define ETH_ALEN            6
#define ETH_MTU             1500
#define ETH_FRAME_LEN       1514

#define NETDEV_MAX          8

/* MAC Address Type */
typedef uint8_t mac_addr_t[ETH_ALEN];

/* IPv4 Address Type (Network Byte Order) */
typedef struct {
    uint32_t addr;
} ipv4_addr_t;

/* Byte Order Conversion Helpers */
static inline uint16_t htons(uint16_t val) {
    return (uint16_t)(((val & 0xFF) << 8) | ((val >> 8) & 0xFF));
}

static inline uint16_t ntohs(uint16_t val) {
    return htons(val);
}

static inline uint32_t htonl(uint32_t val) {
    return (((val & 0x000000FFU) << 24) |
            ((val & 0x0000FF00U) << 8)  |
            ((val & 0x00FF0000U) >> 8)  |
            ((val & 0xFF000000U) >> 24));
}

static inline uint32_t ntohl(uint32_t val) {
    return htonl(val);
}

/* IPv4 String Helpers */
uint32_t ip_parse(const char *str);
void     ip_format(uint32_t ip, char *buf, size_t buf_len);
void     mac_format(const mac_addr_t mac, char *buf, size_t buf_len);

/* Forward declaration */
struct net_device;

/* Network Device Interface */
typedef struct net_device {
    char        name[16];

    mac_addr_t  mac;
    ipv4_addr_t ip_addr;
    ipv4_addr_t netmask;
    ipv4_addr_t gateway;
    ipv4_addr_t dns;

    uint32_t    mtu;
    bool        is_up;

    int       (*init)(struct net_device *dev);
    int       (*send)(struct net_device *dev, const void *data, size_t len);

    void       *driver_data;

    /* Interface Statistics */
    uint64_t    rx_packets;
    uint64_t    tx_packets;
    uint64_t    rx_bytes;
    uint64_t    tx_bytes;
    uint64_t    rx_errors;
    uint64_t    tx_errors;
} net_device_t;

/* Device Management APIs */
void          netdev_init(void);
int           netdev_register(net_device_t *dev);
net_device_t *netdev_get_by_name(const char *name);
net_device_t *netdev_get_default(void);
size_t        netdev_get_count(void);
net_device_t *netdev_get_by_index(size_t idx);

/* Entry point called by drivers when a raw Ethernet frame is received */
int           netdev_receive(net_device_t *dev, const void *data, size_t len);

#endif /* NYOTA_NET_NETDEV_H */
