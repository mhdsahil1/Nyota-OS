# Nyota OS Advanced Networking Architecture

**Target Release**: Nyota OS v1.3.0  
**Architecture**: x86_64 Long Mode (SMP-aware)  
**Status**: Production / Tested  

---

## 1. Network Subsystem Overview

Nyota OS v1.3.0 upgrades the networking stack into a reliable, configurable, secure, and testable network architecture. The stack operates across the kernel and userland with clean layer abstractions:

```
+-------------------------------------------------------------------------+
| User Applications (httpget, httpd, dhclient, nslookup, ifconfig, nyfw)  |
+-------------------------------------------------------------------------+
                                    | Syscalls (socket, net_ctrl, firewall_op)
+-------------------------------------------------------------------------+
| Socket Subsystem (STREAM / DGRAM, TCP / UDP / ICMP, timeout handling)   |
+-------------------------------------------------------------------------+
| Transport Layer: TCP (Retransmit queue, fast retransmit, window adv)    |
|                  UDP (Port matching, broadcast dispatch, bounded RX)   |
+-------------------------------------------------------------------------+
| Network Layer:   IPv4 (Checksum, fragmentation rejection, forwarding)   |
|                  IPv6 (128-bit addresses, SLAAC, ICMPv6, NDP)          |
|                  Firewall Engine (Ingress / Egress packet filtering)    |
|                  Routing Engine (Longest-prefix match, default routes)  |
+-------------------------------------------------------------------------+
| Link Layer:      Ethernet Framing (IPv4, IPv6, ARP, Multicast MAC)      |
|                  ARP Cache (IPv4 address resolution)                    |
|                  NDP Cache (IPv6 Neighbor Discovery)                    |
+-------------------------------------------------------------------------+
| Network Devices: Netdev Abstraction (`netdev_t`, link state, stats)     |
| Hardware Driver: Intel E1000 Gigabit Ethernet (82540EM PCI Controller)  |
+-------------------------------------------------------------------------+
```

---

## 2. Network-Device Abstraction (`netdev`)

Located in [include/net/netdev.h](file:///c:/Users/HP/Projects/Nyota%20OS/include/net/netdev.h) and [kernel/net/netdev.c](file:///c:/Users/HP/Projects/Nyota%20OS/kernel/net/netdev.c):

### 2.1 Interface Data Structures
Each network adapter registers as a `netdev_t` containing:
* **Interface Name**: e.g., `eth0`, `lo`.
* **Hardware Address**: 6-byte Ethernet MAC (`mac_addr[6]`).
* **MTU**: Configurable Maximum Transmission Unit (default 1500 bytes).
* **Interface Flags & State**: `NETDEV_FLAG_UP`, `NETDEV_FLAG_RUNNING`, `NETDEV_FLAG_LINK_UP`.
* **IPv4 Configuration**: Address, subnet mask, default gateway.
* **IPv6 Configuration**: 128-bit link-local address (`fe80:...`) and global unicast address.
* **Driver Callbacks**: `transmit()`, `receive()`, `set_flags()`.
* **Traffic Statistics**: RX packets, TX packets, RX bytes, TX bytes, RX dropped, TX dropped, errors.

### 2.2 Device Operations & Lifecycle
* **Registration**: `netdev_register(dev)` registers device into the global device table under lock.
* **Link State Management**: Link status detected via driver or hardware link change reporting.
* **State Control**: `netdev_up(dev)` and `netdev_down(dev)` toggle operational states and clear pending queues.
* **Driver Isolation**: Hardware drivers like E1000 don't couple directly to IP protocols; they interact solely via `netdev_transmit()` and pass received frames via `ethernet_receive()`.

---

## 3. Dynamic Configuration via DHCPv4 (`dhclient`)

Located in [user/dhclient/main.c](file:///c:/Users/HP/Projects/Nyota%20OS/user/dhclient/main.c):

### 3.1 Protocol Flow (RFC 2131)
1. **DHCPDISCOVER**: Client broadcasts UDP packet from port 68 to port 67 (`255.255.255.255:67`) with hardware MAC, requested parameters (Subnet Mask, Router, DNS Server), and unique 32-bit transaction ID (XID).
2. **DHCPOFFER**: Server responds with offered IP address, subnet mask, default gateway, and DNS servers.
3. **DHCPREQUEST**: Client confirms the lease offer by requesting the offered IP and specifying the server identifier.
4. **DHCPACK**: Server acknowledges the lease, confirming lease duration and renewal timers.

### 3.2 Key Robustness Guarantees
* **Broadcast Transmission**: In [kernel/net/ipv4.c](file:///c:/Users/HP/Projects/Nyota%20OS/kernel/net/ipv4.c), destination IP `0xFFFFFFFF` bypasses ARP cache lookup and automatically maps to Ethernet broadcast MAC `FF:FF:FF:FF:FF:FF`.
* **Broadcast Reception**: In [kernel/net/socket.c](file:///c:/Users/HP/Projects/Nyota%20OS/kernel/net/socket.c), `socket_find_udp()` matches incoming UDP packets addressed to `0xFFFFFFFF` to sockets bound to `0.0.0.0` or matching port.
* **Receive Timeout**: `socket_recv()` enforces a 1.5-second bounded wait to prevent userland client lockups on packet loss.
* **Automatic Kernel Configuration**: Upon acquiring a lease, `dhclient` invokes `sys_net_ctrl(NET_CTRL_SET_IPV4, ...)` to configure IP, netmask, gateway, and default routing table entries, and updates `/etc/resolv.conf`.

---

## 4. Routing Subsystem

Located in [include/net/route.h](file:///c:/Users/HP/Projects/Nyota%20OS/include/net/route.h) and [kernel/net/route.c](file:///c:/Users/HP/Projects/Nyota%20OS/kernel/net/route.c):

### 4.1 Routing Table Architecture
* **Table Capacity**: 32 route entries (`route_t`).
* **Route Types**:
  - `ROUTE_CONNECTED`: Directly attached subnet (e.g., `10.0.2.0/24 -> eth0`). Next-hop gateway is `0.0.0.0` (direct).
  - `ROUTE_GATEWAY`: Remote destination requiring next-hop gateway forwarding (e.g., `0.0.0.0/0 -> 10.0.2.2 via eth0`).
* **Route Lookup**: Evaluates all entries with bitwise subnet mask match:
  $$(dest\_ip \ \& \ route->netmask) == route->dest\_ip$$
  Implements **Longest-Prefix Match (LPM)**: The route with the highest prefix length (most specific netmask) is selected.
* **Control APIs**:
  - `route_add(dest, netmask, gateway, metric, dev)`
  - `route_del(dest, netmask)`
  - `route_get_entries(buf, max_count)` exported to userland via `sys_net_ctrl()`.
* **CLI Utility**: `ip route` and `ip route add/del` ([user/ip/main.c](file:///c:/Users/HP/Projects/Nyota%20OS/user/ip/main.c)).

---

## 5. DNS Resolver Improvements

Located in [user/nslookup/main.c](file:///c:/Users/HP/Projects/Nyota%20OS/user/nslookup/main.c):

### 5.1 Enhancements
* **Local Hosts Lookup**: Queries `/etc/hosts` prior to network lookup for instant deterministic name resolution.
* **Resolver Configuration**: Parses nameservers dynamically from `/etc/resolv.conf` (e.g., `nameserver 10.0.2.3`).
* **Record Types**: Supports DNS Type `A` (IPv4) and Type `AAAA` (IPv6).
* **Compression Pointer Protection**: Recursion-bounded compression pointer decompression (`0xC0` prefix) validates offset boundaries against the packet buffer, preventing infinite decompression loops and buffer over-reads.

---

## 6. TCP Reliability Architecture

Located in [include/net/tcp.h](file:///c:/Users/HP/Projects/Nyota%20OS/include/net/tcp.h) and [kernel/net/tcp.c](file:///c:/Users/HP/Projects/Nyota%20OS/kernel/net/tcp.c):

### 6.1 State Machine & Transitions
Tracks full RFC 793 connection states:
`CLOSED` $\to$ `SYN_SENT` $\to$ `SYN_RECEIVED` $\to$ `ESTABLISHED` $\to$ `FIN_WAIT_1` $\to$ `FIN_WAIT_2` $\to$ `CLOSING` $\to$ `TIME_WAIT` $\to$ `CLOSED`.

### 6.2 Retransmission & Flow Control
* **Retransmission Queue**: Outgoing unacknowledged segments are buffered in a per-connection retransmit queue (`tcp_retransmit_entry_t`).
* **Exponential Backoff**: Initial retransmit timeout (RTO) starts at 1000 ms. On timeout, RTO doubles up to a maximum cap (16000 ms). Up to 5 retries before dropping the connection.
* **Fast Retransmit**: Duplicate ACKs are tracked (`dup_ack_count`). Receiving 3 consecutive duplicate ACKs immediately retransmits the earliest unacknowledged segment without waiting for the retransmit timer to expire.
* **Window Flow Control**: Transmitted segments advertise the socket's actual available receive buffer space (`TCP_RX_BUF_SIZE - rx_count`), preventing buffer exhaustion at the receiver.
* **TIME_WAIT Expiration**: Sockets in `TIME_WAIT` transition cleanly to `CLOSED` after `2 * MSL` ticks in `tcp_timer_tick()`.

---

## 7. Diagnostics & Management

* **`netinfo`**: High-level network summary showing interface count, IPv4, IPv6, Gateway, DNS, TCP/UDP availability, and firewall status.
* **`ifconfig`**: Displays interface MAC, IPv4, IPv6, MTU, flags, and accurate RX/TX packet and byte counters.
* **`ip`**: Manages routing tables (`ip route`), link status (`ip link set <dev> up/down`), and address assignment (`ip addr add`).
* **`netstat`**: Lists active sockets, listening ports, remote connections, and connection states.
* **`ping`**: ICMP echo requests for latency and packet loss verification.

---

## 8. QEMU Testing & Execution

Nyota OS is tested under QEMU user-mode (SLIRP) networking:
```bash
qemu-system-x86_64 -m 512M -smp 4 \
  -drive file=build/disk.img,format=raw,if=ide \
  -netdev user,id=net0,hostfwd=tcp::8080-:80 \
  -device e1000,netdev=net0,mac=52:54:00:12:34:56
```
* Virtual Router / DHCP: `10.0.2.2`
* Virtual DNS Server: `10.0.2.3`
* Host port forward: Host port `8080` $\to$ Guest port `80`
