/* =============================================================================
 * Nyota OS — Capability & Privilege Model Implementation (Phase 8)
 * Checks process capabilities, privilege boundaries, and access rights.
 * =========================================================================== */

#include "security/capability.h"
#include "process.h"

bool has_capability(struct process *proc, uint64_t cap) {
    if (!proc) return false;
    /* Root process (UID 0) has all capabilities */
    if (proc->uid == 0) return true;
    return (proc->capabilities & cap) == cap;
}

bool can_signal_process(struct process *sender, struct process *target) {
    if (!sender || !target) return false;
    /* Root or CAP_KILL can signal any process */
    if (sender->uid == 0 || has_capability(sender, CAP_KILL)) return true;
    /* Processes with identical UID can signal each other */
    return sender->uid == target->uid;
}
