# Nyota OS Packet-Filtering Firewall (`nyfw`)

**Target Release**: Nyota OS v1.3.0  
**Architecture**: x86_64 Long Mode (SMP-aware)  
**Status**: Production / Tested  

---

## 1. Overview

Nyota OS v1.3.0 implements a kernel-level packet-filtering firewall engine in [include/net/firewall.h](file:///c:/Users/HP/Projects/Nyota%20OS/include/net/firewall.h) and [kernel/net/firewall.c](file:///c:/Users/HP/Projects/Nyota%20OS/kernel/net/firewall.c).

The firewall provides configurable rule-based packet inspection for both IPv4 and IPv6 traffic, default policy control, traffic counters, and a privileged userland management tool: `nyfw`.

---

## 2. Kernel Engine Architecture

### 2.1 Packet Processing Hooks
The firewall is embedded directly into the IP processing pathways:
1. **Ingress Filtering**: Executed in `ipv4_receive()` and `ipv6_receive()` before packet headers are stripped or passed to transport protocol handlers.
2. **Egress Filtering**: Executed in `ipv4_send()` and `ipv6_send()` before packets are encapsulated in Ethernet frames.

```
       [ Ingress Packet ]                   [ Egress Packet ]
               |                                    |
     Header Validation                    Route & Header Setup
               |                                    |
   +-----------------------+            +-----------------------+
   | Firewall Ingress Hook |            | Firewall Egress Hook  |
   | (Direction: IN)       |            | (Direction: OUT)      |
   +-----------------------+            +-----------------------+
        /            \                       /            \
    [ACCEPT]        [DROP]               [ACCEPT]        [DROP]
       |              |                     |              |
   Transport      Discard &             Ethernet       Discard &
    Handler      Update Drops           Transmit      Update Drops
```

### 2.2 Rule Specification (`firewall_rule_t`)
Each rule contains:
```c
typedef struct {
    uint32_t id;                        /* Unique rule identifier */
    uint8_t  direction;                 /* FW_DIR_IN, FW_DIR_OUT, or FW_DIR_BOTH */
    uint8_t  action;                    /* FW_ACTION_ACCEPT or FW_ACTION_DROP */
    uint8_t  protocol;                  /* IPPROTO_TCP, IPPROTO_UDP, IPPROTO_ICMP, or 0 (any) */
    uint8_t  ip_version;                /* 4 = IPv4, 6 = IPv6, 0 = any */
    
    /* IPv4 Filtering */
    uint32_t src_ip;
    uint32_t src_mask;
    uint32_t dest_ip;
    uint32_t dest_mask;
    
    /* Port Ranges (Host Byte Order) */
    uint16_t src_port_min;
    uint16_t src_port_max;
    uint16_t dest_port_min;
    uint16_t dest_port_max;
    
    /* Interface Binding */
    char ifname[16];
    
    /* Execution Telemetry */
    uint64_t packet_count;
    uint64_t byte_count;
} firewall_rule_t;
```

---

## 3. Evaluation Rules & Mechanics

1. **Rule Ordering**: Rules are evaluated sequentially in the order they were inserted.
2. **First Match Wins**: As soon as a packet matches all criteria of a rule, that rule's action (`ACCEPT` or `DROP`) is immediately executed, its telemetry counters are incremented, and rule evaluation stops.
3. **Default Policy**: If no rule matches the packet, the default policy for that direction is applied:
   - `default_in_policy`: Typically `FW_ACTION_ACCEPT` (or `FW_ACTION_DROP` in hardened mode).
   - `default_out_policy`: Typically `FW_ACTION_ACCEPT`.
4. **Bypass on Disabled**: If the firewall is disabled (`enabled == false`), all packets are accepted without evaluation overhead.

---

## 4. Syscall Interface (`sys_firewall_op`)

Located in [kernel/arch/x86_64/syscall.c](file:///c:/Users/HP/Projects/Nyota%20OS/kernel/arch/x86_64/syscall.c) as syscall 78:
```c
int64_t sys_firewall_op(int op, void *arg, size_t arg_size);
```

### Supported Operations
| Opcode | Operation | Description |
|---|---|---|
| `FW_OP_GET_STATUS` (0) | Query Status | Returns active state, rule count, policies, and drop/accept counters. |
| `FW_OP_ADD_RULE` (1) | Add Rule | Appends a validated rule to the active chain (requires root). |
| `FW_OP_DEL_RULE` (2) | Delete Rule | Removes a rule matching specified rule ID (requires root). |
| `FW_OP_FLUSH_RULES` (3) | Flush Chain | Removes all configured rules (requires root). |
| `FW_OP_SET_POLICY` (4) | Set Policy | Sets default inbound or outbound action (requires root). |
| `FW_OP_ENABLE` (5) | Enable | Activates packet inspection (requires root). |
| `FW_OP_DISABLE` (6) | Disable | Deactivates packet inspection (requires root). |

### Security Checks
- **Privilege Validation**: Operations that modify firewall configuration (1 through 6) require the caller to possess UID 0 (root). Unprivileged processes receive `SYS_ERR_EPERM`.
- **User Memory Copying**: All pointers are validated with `user_validate_pointer()` and transferred using safe `copy_from_user()` / `copy_to_user()`.

---

## 5. Userland Command (`nyfw`)

Located in [user/nyfw/main.c](file:///c:/Users/HP/Projects/Nyota%20OS/user/nyfw/main.c):

### 5.1 Status & Listing
```bash
# Query active firewall state
$ nyfw status
Firewall: ENABLED
Default Inbound  : ACCEPT
Default Outbound : ACCEPT
Active Rules     : 2
Packets Accepted : 428
Packets Dropped  : 14

# List all configured rules
$ nyfw list
ID  Dir  Act   Proto  Src IP/Mask        Dst IP/Mask        Ports     Packets
1   IN   DROP  TCP    0.0.0.0/0          0.0.0.0/0          *:23      14
2   IN   ACCEPT TCP   0.0.0.0/0          0.0.0.0/0          *:80      102
```

### 5.2 Rule Management
```bash
# Block inbound Telnet (port 23)
$ nyfw add in drop tcp 0.0.0.0/0 0.0.0.0/0 23

# Allow inbound HTTP (port 80)
$ nyfw add in accept tcp 0.0.0.0/0 0.0.0.0/0 80

# Delete a rule by ID
$ nyfw del 1

# Change default inbound policy to drop
$ nyfw policy in drop

# Disable and re-enable firewall
$ nyfw disable
$ nyfw enable

# Flush all rules
$ nyfw flush
```
