# Nyota OS IPv6 Subsystem Specification

**Target Release**: Nyota OS v1.3.0  
**Architecture**: x86_64 Long Mode (SMP-aware)  
**Status**: Production / Tested  

---

## 1. Overview

Nyota OS v1.3.0 introduces an initial IPv6 network stack implemented in [include/net/ipv6.h](file:///c:/Users/HP/Projects/Nyota%20OS/include/net/ipv6.h), [kernel/net/ipv6.c](file:///c:/Users/HP/Projects/Nyota%20OS/kernel/net/ipv6.c), [include/net/icmpv6.h](file:///c:/Users/HP/Projects/Nyota%20OS/include/net/icmpv6.h), and [kernel/net/icmpv6.c](file:///c:/Users/HP/Projects/Nyota%20OS/kernel/net/icmpv6.c).

The stack provides full 128-bit address handling, Stateless Address Autoconfiguration (SLAAC), ICMPv6 echo handling, Neighbor Discovery Protocol (NDP), solicited-node multicast, and userland address querying.

---

## 2. Protocol Framing & Data Structures

### 2.1 IPv6 Header Format (RFC 8200)
```c
typedef struct {
    uint32_t vtc_fl;        /* Version (4 bits), Traffic Class (8 bits), Flow Label (20 bits) */
    uint16_t payload_len;   /* Length of payload following header (Big Endian) */
    uint8_t  next_header;   /* Protocol identifier (TCP=6, UDP=17, ICMPv6=58) */
    uint8_t  hop_limit;     /* Hop limit (decremented by routers) */
    ipv6_addr_t src_ip;     /* 128-bit source IP address */
    ipv6_addr_t dest_ip;    /* 128-bit destination IP address */
} __attribute__((packed)) ipv6_header_t;
```

### 2.2 Address Representation (`ipv6_addr_t`)
The 128-bit address is represented as a union of bytes, 16-bit words, and 32-bit dwords:
```c
typedef union {
    uint8_t  bytes[16];
    uint16_t words[8];
    uint32_t dwords[4];
} ipv6_addr_t;
```
Standard string formatting (`inet_ntop(AF_INET6, ...)`) compresses consecutive blocks of zeroes using standard `::` notation (e.g. `fe80::5054:ff:fe12:3456`).

---

## 3. Address Autoconfiguration (SLAAC)

In accordance with RFC 4291 and RFC 4862, every network adapter generates a link-local IPv6 address using Modified EUI-64 format derived from its 48-bit Ethernet MAC:
1. The 6-byte MAC address `MM:MM:MM:SS:SS:SS` is split into halves.
2. The byte pattern `0xFF:0xFE` is inserted between the two halves.
3. The Universal/Local bit (bit 1 of the first byte) is inverted (`mac[0] ^ 0x02`).
4. The prefix `fe80::/64` is prepended to form the 128-bit link-local address:
   `fe80::[mac[0]^2][mac[1]]:[mac[2]]ff:fe[mac[3]]:[mac[4]][mac[5]]`

For an E1000 device with MAC `52:54:00:12:34:56`:
- Modified EUI-64: `50:54:00:ff:fe:12:34:56`
- Resulting IPv6 Link-Local: `fe80::5054:ff:fe12:3456`

---

## 4. Ethernet Multicast Mapping

In contrast to IPv4 which uses ARP broadcast, IPv6 uses multicast mapping (RFC 2464):
* **Solicited-Node Multicast**: Generated for any IPv6 address by appending the low-order 24 bits to the prefix `ff02::1:ff00:0/104`.
* **Multicast MAC Translation**: The low-order 32 bits of any IPv6 destination multicast address are mapped directly into an Ethernet MAC with prefix `33:33`:
  ```c
  mac[0] = 0x33;
  mac[1] = 0x33;
  mac[2] = ip->bytes[12];
  mac[3] = ip->bytes[13];
  mac[4] = ip->bytes[14];
  mac[5] = ip->bytes[15];
  ```
Ethernet frames carrying EtherType `0x86DD` addressed to `33:33:*` are accepted by the link layer without filtering.

---

## 5. Neighbor Discovery Protocol (NDP) & ICMPv6

NDP (RFC 4861) replaces IPv4 ARP with ICMPv6 messages:

### 5.1 ICMPv6 Message Types
* `ICMPV6_TYPE_ECHO_REQUEST` (128): Echo request responder formats and transmits `ICMPV6_TYPE_ECHO_REPLY` (129) preserving the identifier, sequence number, and data payload.
* `ICMPV6_TYPE_NEIGHBOR_SOLICIT` (135): Requests the link-layer address of a target IPv6 host.
* `ICMPV6_TYPE_NEIGHBOR_ADVERT` (136): Advertises the link-layer MAC address. The target MAC is added to the kernel NDP Cache.
* `ICMPV6_TYPE_ROUTER_SOLICIT` (133) & `ROUTER_ADVERT` (134): Discovers routers and network prefixes.

### 5.2 NDP Cache
Maintains a 32-entry lookup table mapping `ipv6_addr_t` to 6-byte Ethernet MACs with timestamp and state (`NDP_STATE_INCOMPLETE`, `NDP_STATE_REACHABLE`, `NDP_STATE_STALE`).

---

## 6. Checksum Calculation

IPv6 packets require an upper-layer checksum over a pseudo-header:
```
+---------------------------------------------------+
| 128-bit Source IPv6 Address                       |
+---------------------------------------------------+
| 128-bit Destination IPv6 Address                  |
+---------------------------------------------------+
| Upper-Layer Packet Length (32 bits)               |
+---------------------------------------------------+
| Zero (24 bits)               | Next Header (8 bits)|
+---------------------------------------------------+
| Upper-Layer Payload (ICMPv6 / UDP / TCP)          |
+---------------------------------------------------+
```
Implemented in `ipv6_checksum()` in [kernel/net/ipv6.c](file:///c:/Users/HP/Projects/Nyota%20OS/kernel/net/ipv6.c).

---

## 7. Transport Integration & Verification

* **Packet Ingress**: Frames matching EtherType `0x86DD` are validated for length $\ge 40$ bytes, payload length consistency, and hop limit before passing to the firewall engine.
* **Firewall Hook**: `firewall_filter_packet(NETDEV_DIR_IN, IPPROTO_*, ...)` checks IPv6 source and destination addresses against the ruleset before dispatching to upper layers.
* **DNS Resolution**: `nslookup` supports querying DNS Type `AAAA` records for IPv6 addresses.
* **Diagnostics**: `ifconfig` and `netinfo` report active IPv6 configuration.

### Known Limitations
* Advanced extension header chains (Hop-by-Hop, Routing, Fragment headers) are safely dropped to prevent fragmentation-based attacks.
* Dynamic stateful DHCPv6 is not yet implemented; IPv6 address assignment uses SLAAC and static configuration.
