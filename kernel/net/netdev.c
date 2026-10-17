/* =============================================================================
 * Nyota OS — Network Device Subsystem & Loopback Adapter
 * =========================================================================== */

#include "net/netdev.h"
#include "net/ethernet.h"
#include "net/ipv4.h"
#include "net/ipv6.h"
#include "memory.h"
#include "serial.h"
#include "timer.h"
#include "syscall.h"

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
    pktmon_record_packet(dev ? dev->name : "lo", 1, data, len);

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
    loopback_dev.link_up      = true;
    loopback_dev.flags        = NETDEV_FLAG_UP | NETDEV_FLAG_RUNNING | NETDEV_FLAG_LOOPBACK;

    /* IPv6 Loopback: ::1 */
    loopback_dev.ip6_addr[15] = 1;
    loopback_dev.has_ip6      = true;

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
    dev->link_up = true;
    dev->flags |= NETDEV_FLAG_UP | NETDEV_FLAG_RUNNING;

    if (strcmp(dev->name, "lo") != 0) {
        dev->flags |= NETDEV_FLAG_BROADCAST | NETDEV_FLAG_MULTICAST;
        /* Auto-generate SLAAC IPv6 link-local address if MAC is valid and IPv6 not configured */
        if (!dev->has_ip6) {
            ipv6_generate_slaac(dev->mac, (ipv6_addr_t *)dev->ip6_addr);
            dev->has_ip6 = true;
        }
    }

    serial_write("[NET] Registered interface: ");
    serial_write(dev->name);
    serial_write("\n");

    return 0;
}

int netdev_unregister(net_device_t *dev) {
    if (!dev) return -1;
    for (size_t i = 0; i < netdev_count; i++) {
        if (netdev_list[i] == dev) {
            for (size_t j = i; j < netdev_count - 1; j++) {
                netdev_list[j] = netdev_list[j + 1];
            }
            netdev_list[--netdev_count] = NULL;
            return 0;
        }
    }
    return -1;
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

int netdev_set_up(net_device_t *dev) {
    if (!dev) return -1;
    dev->is_up = true;
    dev->flags |= NETDEV_FLAG_UP;
    return 0;
}

int netdev_set_down(net_device_t *dev) {
    if (!dev) return -1;
    dev->is_up = false;
    dev->flags &= ~NETDEV_FLAG_UP;
    return 0;
}

int netdev_receive(net_device_t *dev, const void *data, size_t len) {
    if (!dev || !data || len == 0) return -1;

    dev->rx_packets++;
    dev->rx_bytes += len;
    pktmon_record_packet(dev ? dev->name : "eth0", 0, data, len);

    return ethernet_receive(dev, data, len);
}

/* ── Packet Monitor Ring Buffer & APIs (Phase 18) ─────────────────────────── */

#define PKTMON_RING_MAX 128
static pkt_record_t pktmon_ring[PKTMON_RING_MAX];
static uint32_t pktmon_head = 0;
static uint32_t pktmon_count = 0;
static bool pktmon_active = false;

int pktmon_start(void) {
    pktmon_active = true;
    return 0;
}

int pktmon_stop(void) {
    pktmon_active = false;
    return 0;
}

void pktmon_record_packet(const char *ifname, uint8_t direction, const void *data, size_t len) {
    if (!pktmon_active || !data || len < 14) return;

    const uint8_t *raw = (const uint8_t *)data;
    uint32_t idx = pktmon_head % PKTMON_RING_MAX;
    pkt_record_t *rec = &pktmon_ring[idx];
    memset(rec, 0, sizeof(pkt_record_t));

    rec->timestamp = timer_ticks();
    rec->direction = direction;
    rec->packet_len = (uint32_t)len;

    if (ifname) {
        size_t slen = strlen(ifname);
        if (slen >= sizeof(rec->ifname)) slen = sizeof(rec->ifname) - 1;
        memcpy(rec->ifname, ifname, slen);
        rec->ifname[slen] = '\0';
    }

    /* Parse Ethernet type */
    uint16_t eth_proto = (raw[12] << 8) | raw[13];
    rec->eth_proto = eth_proto;

    /* IPv4 parsing */
    if (eth_proto == 0x0800 && len >= 34) {
        rec->ip_proto = raw[23];
        memcpy(&rec->src_ip, &raw[26], 4);
        memcpy(&rec->dst_ip, &raw[30], 4);

        if (rec->ip_proto == 6 && len >= 54) { /* TCP */
            rec->src_port = (raw[34] << 8) | raw[35];
            rec->dst_port = (raw[36] << 8) | raw[37];
            rec->tcp_flags = raw[47];
        } else if (rec->ip_proto == 17 && len >= 42) { /* UDP */
            rec->src_port = (raw[34] << 8) | raw[35];
            rec->dst_port = (raw[36] << 8) | raw[37];
        }
    }

    pktmon_head++;
    if (pktmon_count < PKTMON_RING_MAX) {
        pktmon_count++;
    }
}

int pktmon_read_records(void *buf, size_t max_count) {
    if (!buf || max_count == 0) return 0;
    pkt_record_t *out = (pkt_record_t *)buf;

    size_t avail = (pktmon_count < max_count) ? pktmon_count : max_count;
    if (avail == 0) return 0;

    uint32_t start_idx = (pktmon_head >= pktmon_count) ? (pktmon_head - pktmon_count) : 0;
    for (size_t i = 0; i < avail; i++) {
        uint32_t idx = (start_idx + i) % PKTMON_RING_MAX;
        memcpy(&out[i], &pktmon_ring[idx], sizeof(pkt_record_t));
    }

    pktmon_count -= (uint32_t)avail;
    return (int)avail;
}

int netdev_get_statistics(void *buf, size_t max_count) {
    if (!buf || max_count == 0) return 0;
    net_if_stat_t *out = (net_if_stat_t *)buf;

    size_t count = 0;
    for (size_t i = 0; i < netdev_count && count < max_count; i++) {
        net_device_t *dev = netdev_list[i];
        if (!dev) continue;

        memset(&out[count], 0, sizeof(net_if_stat_t));
        size_t slen = strlen(dev->name);
        if (slen >= sizeof(out[count].ifname)) slen = sizeof(out[count].ifname) - 1;
        memcpy(out[count].ifname, dev->name, slen);
        out[count].ifname[slen] = '\0';

        out[count].rx_packets = dev->rx_packets;
        out[count].rx_bytes = dev->rx_bytes;
        out[count].tx_packets = dev->tx_packets;
        out[count].tx_bytes = dev->tx_bytes;
        out[count].is_up = dev->is_up ? 1 : 0;
        out[count].mtu = dev->mtu;
        count++;
    }

    return (int)count;
}

