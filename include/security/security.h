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

void security_init(void);
void security_log(const char *subsys, uint32_t pid);
int64_t kernel_secinfo(void *info);

#endif /* NYOTA_SECURITY_SECURITY_H */
