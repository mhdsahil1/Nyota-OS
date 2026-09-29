/* =============================================================================
 * Nyota OS — Capability & Privilege Model (Phase 8)
 * Capability definitions, permission flags, and validation checks.
 * =========================================================================== */

#ifndef NYOTA_SECURITY_CAPABILITY_H
#define NYOTA_SECURITY_CAPABILITY_H

#include "types.h"

/* Privilege Capabilities */
#define CAP_NET_ADMIN      (1ULL << 0)  /* Network interface & route configuration */
#define CAP_NET_RAW        (1ULL << 1)  /* Raw socket and packet creation */
#define CAP_SYS_ADMIN      (1ULL << 2)  /* System administration, mount, reboot */
#define CAP_IPC_OWNER      (1ULL << 3)  /* Bypass IPC permission checks */
#define CAP_KILL           (1ULL << 4)  /* Signal processes with different UID */
#define CAP_SETUID         (1ULL << 5)  /* Change process UID/GID */
#define CAP_CHOWN          (1ULL << 6)  /* Change file ownership */
#define CAP_ALL            0xFFFFFFFFFFFFFFFFULL

struct process;

bool has_capability(struct process *proc, uint64_t cap);
bool can_signal_process(struct process *sender, struct process *target);

#endif /* NYOTA_SECURITY_CAPABILITY_H */
