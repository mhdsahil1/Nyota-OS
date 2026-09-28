/* =============================================================================
 * Nyota OS — Network Device Subsystem & Loopback Adapter
 * =========================================================================== */

#include "net/netdev.h"
#include "net/ethernet.h"
#include "net/ipv4.h"
#include "memory.h"
#include "serial.h"

static net_device_t *netdev_list[NETDEV_MAX];
static size_t netdev_count = 0;

static net_device_t loopback_dev;

/* ── String / Address Helpers ─────────────────────────────────────────────── */

uint32_t ip_parse(const char *str) {
    if (!str) return 0;
    uint32_t parts[4] = {0};
    int idx = 0;

    while (*str && idx < 4) {
        if (*str >= '0' && *str <= '9') {
            parts[idx] = parts[idx] * 10 + (*str - '0');
        } else if (*str == '.') {
            idx++;
        } else {
            break;
        }
        str++;
    }

    if (idx != 3) return 0;

    return ((parts[0] & 0xFF) |
           ((parts[1] & 0xFF) << 8) |
           ((parts[2] & 0xFF) << 16) |
           ((parts[3] & 0xFF) << 24));
}

void ip_format(uint32_t ip, char *buf, size_t buf_len) {
    if (!buf || buf_len < 16) return;

    uint8_t b1 = ip & 0xFF;
    uint8_t b2 = (ip >> 8) & 0xFF;
    uint8_t b3 = (ip >> 16) & 0xFF;
    uint8_t b4 = (ip >> 24) & 0xFF;

    char *p = buf;
    (void)buf_len;

    uint8_t bytes[4] = {b1, b2, b3, b4};
    for (int i = 0; i < 4; i++) {
        uint8_t val = bytes[i];
        if (val >= 100) {
            *p++ = (char)('0' + (val / 100));
            *p++ = (char)('0' + ((val / 10) % 10));
            *p++ = (char)('0' + (val % 10));
        } else if (val >= 10) {
            *p++ = (char)('0' + (val / 10));
            *p++ = (char)('0' + (val % 10));
        } else {
            *p++ = (char)('0' + val);
        }
        if (i < 3) {
            *p++ = '.';
        }
    }
    *p = '\0';
}

void mac_format(const mac_addr_t mac, char *buf, size_t buf_len) {
    if (!buf || buf_len < 18) return;
    const char hex_chars[] = "0123456789abcdef";

    char *p = buf;
    for (int i = 0; i < ETH_ALEN; i++) {
        *p++ = hex_chars[(mac[i] >> 4) & 0x0F];
        *p++ = hex_chars[mac[i] & 0x0F];
        if (i < ETH_ALEN - 1) {
            *p++ = ':';
        }
    }
    *p = '\0';
}

/* ── Loopback Adapter Implementation ──────────────────────────────────────── */

static int loopback_send(net_device_t *dev, const void *data, size_t len) {
    if (!dev || !data || len == 0) return -1;

    dev->tx_packets++;
    dev->tx_bytes += len;

    /* Loopback directly receives its own transmitted packet */
    return ipv4_receive(dev, data, len);
}

static void loopback_init(void) {
    memset(&loopback_dev, 0, sizeof(net_device_t));

    memcpy(loopback_dev.name, "lo", 3);
    loopback_dev.ip_addr.addr = ip_parse("127.0.0.1");
    loopback_dev.netmask.addr = ip_parse("255.0.0.0");
    loopback_dev.gateway.addr = 0;
    loopback_dev.dns.addr     = 0;
    loopback_dev.mtu          = 65536;
    loopback_dev.is_up        = true;
    loopback_dev.send         = loopback_send;

    netdev_register(&loopback_dev);
}

/* ── Device Management APIs ───────────────────────────────────────────────── */

void netdev_init(void) {
    memset(netdev_list, 0, sizeof(netdev_list));
    netdev_count = 0;

    loopback_init();
}

int netdev_register(net_device_t *dev) {
    if (!dev || netdev_count >= NETDEV_MAX) {
        return -1;
    }

    netdev_list[netdev_count++] = dev;
    dev->is_up = true;

    serial_write("[NET] Registered interface: ");
    serial_write(dev->name);
    serial_write("\n");

    return 0;
}

net_device_t *netdev_get_by_name(const char *name) {
    if (!name) return NULL;
    for (size_t i = 0; i < netdev_count; i++) {
        if (strcmp(netdev_list[i]->name, name) == 0) {
            return netdev_list[i];
        }
    }
    return NULL;
}

net_device_t *netdev_get_default(void) {
    /* Prefer eth0, fallback to lo */
    net_device_t *eth = netdev_get_by_name("eth0");
    if (eth && eth->is_up) return eth;
    return &loopback_dev;
}

size_t netdev_get_count(void) {
    return netdev_count;
}

net_device_t *netdev_get_by_index(size_t idx) {
    if (idx >= netdev_count) return NULL;
    return netdev_list[idx];
}

int netdev_receive(net_device_t *dev, const void *data, size_t len) {
    if (!dev || !data || len == 0) return -1;

    dev->rx_packets++;
    dev->rx_bytes += len;

    return ethernet_receive(dev, data, len);
}
