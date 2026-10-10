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
#include "spinlock.h"
#include "timer.h"
#include "memory.h"

static audit_event_t audit_ring[AUDIT_LOG_CAPACITY];
static size_t audit_head = 0;
static size_t audit_count = 0;
static spinlock_t audit_lock = SPINLOCK_INIT;

void security_init(void) {
    random_init();
    memset(audit_ring, 0, sizeof(audit_ring));
    audit_head = 0;
    audit_count = 0;
}

void security_log(const char *subsys, uint32_t pid) {
    if (!subsys) return;
    audit_log_event(AUDIT_SEV_INFO, subsys, pid, 0, "diagnostic_event");
}

void audit_log_event(uint8_t severity, const char *category, uint32_t pid, uint32_t uid, const char *message) {
    uint64_t rflags = spinlock_acquire_irqsave(&audit_lock);

    audit_event_t *ev = &audit_ring[audit_head];
    ev->timestamp = timer_uptime_sec();
    ev->pid = pid;
    ev->uid = uid;
    ev->severity = severity;
    if (category) {
        strncpy(ev->category, category, sizeof(ev->category) - 1);
        ev->category[sizeof(ev->category) - 1] = '\0';
    } else {
        strcpy(ev->category, "GEN");
    }
    if (message) {
        strncpy(ev->message, message, sizeof(ev->message) - 1);
        ev->message[sizeof(ev->message) - 1] = '\0';
    } else {
        ev->message[0] = '\0';
    }

    audit_head = (audit_head + 1) % AUDIT_LOG_CAPACITY;
    if (audit_count < AUDIT_LOG_CAPACITY) {
        audit_count++;
    }

    spinlock_release_irqrestore(&audit_lock, rflags);

    /* Output to serial console */
    const char *sev_str = "INFO";
    if (severity == AUDIT_SEV_WARN) sev_str = "WARN";
    else if (severity == AUDIT_SEV_DENIED) sev_str = "DENIED";
    else if (severity == AUDIT_SEV_ALERT) sev_str = "ALERT";

    serial_write("[SECURITY] ");
    serial_write(sev_str);
    serial_write(" cat=");
    serial_write(category ? category : "GEN");
    serial_write(" pid=");
    serial_write_dec(pid);
    serial_write(" uid=");
    serial_write_dec(uid);
    serial_write(" msg=");
    serial_write(message ? message : "");
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

int64_t kernel_audit_log_op(uint32_t cmd, void *u_buf, size_t count) {
    process_t *curr = process_get_current();
    if (!curr) return SYS_ERR_EBADF;

    switch (cmd) {
        case AUDIT_CMD_READ: {
            /* Reading audit log requires root or CAP_AUDIT or CAP_SYS_ADMIN */
            if (curr->uid != 0 && curr->euid != 0 && !has_capability(curr, CAP_AUDIT) && !has_capability(curr, CAP_SYS_ADMIN)) {
                audit_log_event(AUDIT_SEV_DENIED, "AUDIT", curr->pid, curr->euid, "unauthorized audit read");
                return SYS_ERR_EPERM;
            }

            if (!u_buf || count == 0) return 0;
            if (count > AUDIT_LOG_CAPACITY) count = AUDIT_LOG_CAPACITY;

            if (!user_validate_pointer(u_buf, sizeof(audit_event_t) * count, true)) {
                return SYS_ERR_EFAULT;
            }

            uint64_t rflags = spinlock_acquire_irqsave(&audit_lock);
            size_t available = audit_count;
            if (count > available) count = available;

            /* Copy latest events */
            audit_event_t temp[AUDIT_LOG_CAPACITY];
            size_t start_idx = (audit_head + AUDIT_LOG_CAPACITY - available) % AUDIT_LOG_CAPACITY;
            for (size_t i = 0; i < count; i++) {
                temp[i] = audit_ring[(start_idx + i) % AUDIT_LOG_CAPACITY];
            }
            spinlock_release_irqrestore(&audit_lock, rflags);

            if (copy_to_user(u_buf, temp, sizeof(audit_event_t) * count) < 0) {
                return SYS_ERR_EFAULT;
            }
            return (int64_t)count;
        }

        case AUDIT_CMD_LOG: {
            if (!u_buf) return SYS_ERR_EINVAL;
            if (!user_validate_pointer(u_buf, sizeof(audit_event_t), false)) {
                return SYS_ERR_EFAULT;
            }
            audit_event_t user_ev;
            if (copy_from_user(&user_ev, u_buf, sizeof(audit_event_t)) < 0) {
                return SYS_ERR_EFAULT;
            }
            audit_log_event(user_ev.severity, user_ev.category, curr->pid, curr->euid, user_ev.message);
            return 0;
        }

        case AUDIT_CMD_CLEAR: {
            if (curr->uid != 0 && curr->euid != 0 && !has_capability(curr, CAP_AUDIT)) {
                return SYS_ERR_EPERM;
            }
            uint64_t rflags = spinlock_acquire_irqsave(&audit_lock);
            audit_head = 0;
            audit_count = 0;
            spinlock_release_irqrestore(&audit_lock, rflags);
            return 0;
        }

        default:
            return SYS_ERR_EINVAL;
    }
}

