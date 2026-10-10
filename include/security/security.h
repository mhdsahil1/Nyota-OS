/* =============================================================================
 * Nyota OS — Security Subsystem & Diagnostic Audit Interface (Phase 8)
 * Security status information, audit event logging, and boundary checks.
 * =========================================================================== */

#ifndef NYOTA_SECURITY_SECURITY_H
#define NYOTA_SECURITY_SECURITY_H

#include "types.h"
#include "security/capability.h"
#include "security/random.h"

typedef struct secinfo {
    bool isolation_enabled;
    bool guard_pages_enabled;
    bool pointer_validation_enabled;
    bool capabilities_enabled;
    bool resource_limits_enabled;
    bool aslr_enabled;
    uint32_t active_processes;
    uint32_t active_pipes;
    uint32_t active_shm_segments;
} secinfo_t;

#define AUDIT_SEV_INFO    0
#define AUDIT_SEV_WARN    1
#define AUDIT_SEV_DENIED  2
#define AUDIT_SEV_ALERT   3

#define AUDIT_CMD_READ    1
#define AUDIT_CMD_LOG     2
#define AUDIT_CMD_CLEAR   3

#define AUDIT_LOG_CAPACITY 64

typedef struct {
    uint64_t timestamp;
    uint32_t pid;
    uint32_t uid;
    uint8_t  severity;
    char     category[16];   /* "AUTH", "CAP", "PERM", "SYSCALL", "MEM", "NET" */
    char     message[80];
} audit_event_t;

void security_init(void);
void security_log(const char *subsys, uint32_t pid);
int64_t kernel_secinfo(void *info);
void audit_log_event(uint8_t severity, const char *category, uint32_t pid, uint32_t uid, const char *message);
int64_t kernel_audit_log_op(uint32_t cmd, void *u_buf, size_t count);

#endif /* NYOTA_SECURITY_SECURITY_H */
