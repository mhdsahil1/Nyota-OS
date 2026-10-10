/* =============================================================================
 * Nyota OS — Security Audit Log Viewer (/bin/auditlog)
 * Displays kernel security audit events. Restricted to root / CAP_AUDIT.
 * =========================================================================== */

#include "libnyota.h"

static const char *sev_to_str(uint8_t sev) {
    switch (sev) {
        case AUDIT_SEV_INFO:   return "INFO";
        case AUDIT_SEV_WARN:   return "WARN";
        case AUDIT_SEV_DENIED: return "DENIED";
        case AUDIT_SEV_ALERT:  return "ALERT";
        default:               return "LOG";
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    audit_event_t events[64];
    int64_t count = audit_log_op(AUDIT_CMD_READ, events, 64);
    if (count < 0) {
        printf("auditlog: permission denied (requires root or CAP_AUDIT)\n");
        return 1;
    }

    if (count == 0) {
        printf("auditlog: no security events recorded.\n");
        return 0;
    }

    printf("=== NYOTA OS SECURITY AUDIT LOG (%lld events) ===\n", count);
    for (int64_t i = 0; i < count; i++) {
        audit_event_t *e = &events[i];
        printf("[%s] [PID:%u UID:%u] [%s] %s\n",
               sev_to_str(e->severity),
               e->pid,
               e->uid,
               e->category[0] ? e->category : "SEC",
               e->message);
    }
    return 0;
}
