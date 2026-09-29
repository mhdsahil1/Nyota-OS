/* =============================================================================
 * Nyota OS — Security Subsystem & Diagnostic Audit Interface (Phase 8)
 * Reports system security state and handles security diagnostic audit logging.
 * =========================================================================== */

#include "security/security.h"
#include "security/random.h"
#include "syscall.h"
#include "serial.h"
#include "vga.h"
#include "process.h"
#include "ipc/pipe.h"
#include "ipc/shm.h"

void security_init(void) {
    random_init();
}

void security_log(const char *subsys, uint32_t pid) {
    if (!subsys) return;
    serial_write("[SEC] PID ");
    serial_write_dec(pid);
    serial_write(": ");
    serial_write(subsys);
    serial_write("\n");
}

int64_t kernel_secinfo(void *info) {
    if (!info) return SYS_ERR_EFAULT;
    if (!user_validate_pointer(info, sizeof(secinfo_t), true)) {
        return SYS_ERR_EFAULT;
    }

    secinfo_t kinfo;
    kinfo.isolation_enabled = true;
    kinfo.guard_pages_enabled = true;
    kinfo.pointer_validation_enabled = true;
    kinfo.capabilities_enabled = true;
    kinfo.resource_limits_enabled = true;
    kinfo.aslr_enabled = true;
    kinfo.active_processes = (uint32_t)process_count();
    kinfo.active_pipes = pipe_active_count();
    kinfo.active_shm_segments = shm_active_count();

    if (copy_to_user(info, &kinfo, sizeof(secinfo_t)) < 0) {
        return SYS_ERR_EFAULT;
    }

    return 0;
}
