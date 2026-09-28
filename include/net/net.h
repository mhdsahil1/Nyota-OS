/* =============================================================================
 * Nyota OS — Central Network Stack Coordinator (net.h)
 * Initializes all network protocol layers and coordinates timer ticks and polling.
 * =========================================================================== */

#ifndef NYOTA_NET_NET_H
#define NYOTA_NET_NET_H

#include "types.h"
#include "net/netdev.h"
#include "net/ethernet.h"
#include "net/arp.h"
#include "net/ipv4.h"
#include "net/icmp.h"
#include "net/udp.h"
#include "net/tcp.h"
#include "net/socket.h"
#include "net/route.h"

void net_init(void);
void net_timer_tick(void);
void net_poll(void);

#endif /* NYOTA_NET_NET_H */
