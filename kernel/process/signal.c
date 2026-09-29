/* =============================================================================
 * Nyota OS — POSIX Signals Implementation (Phase 8)
 * Signal delivery, default actions, exception mapping, and permissions.
 * =========================================================================== */

#include "signal.h"
#include "process.h"
#include "scheduler.h"
#include "security/capability.h"
#include "syscall.h"
#include "serial.h"
#include "gdt.h"

void signal_init_process(process_t *proc) {
    if (!proc) return;
    proc->pending_signals = 0;
    proc->blocked_signals = 0;
    for (int i = 0; i < NSIG; i++) {
        proc->signal_handlers[i] = (uint64_t)SIG_DFL;
    }
}

int signal_send(process_t *target, int sig) {
    if (!target || sig < 1 || sig >= NSIG) {
        return -SYS_ERR_EINVAL;
    }

    process_t *curr = process_get_current();
    if (curr && !can_signal_process(curr, target)) {
        return -SYS_ERR_EPERM;
    }

    if (sig == SIGCONT) {
        target->pending_signals &= ~(1 << SIGSTOP);
        if (target->state == PROCESS_SLEEPING) {
            target->state = PROCESS_READY;
            scheduler_add(target);
        }
    }

    target->pending_signals |= (1 << sig);

    /* Wake target if sleeping in interruptible wait */
    if (target->state == PROCESS_SLEEPING && sig != SIGSTOP) {
        target->state = PROCESS_READY;
        scheduler_add(target);
    }

    return 0;
}

int signal_send_pid(uint32_t pid, int sig) {
    process_t *target = process_find(pid);
    if (!target) return -SYS_ERR_ESRCH;
    return signal_send(target, sig);
}

void signal_check_and_deliver(interrupt_frame_t *frame) {
    if (!frame) return;

    /* Only deliver signals when returning to Ring 3 User Mode */
    if ((frame->cs & 3) != 3) {
        return;
    }

    process_t *curr = process_get_current();
    if (!curr) return;

    uint32_t deliverable = curr->pending_signals & ~curr->blocked_signals;
    if (!deliverable) return;

    for (int sig = 1; sig < NSIG; sig++) {
        if (deliverable & (1 << sig)) {
            curr->pending_signals &= ~(1 << sig);

            if (sig == SIGKILL) {
                serial_write("[SEC] PID "); serial_write_dec(curr->pid);
                serial_write(" killed by SIGKILL\n");
                process_exit(128 + SIGKILL);
                return;
            }

            if (sig == SIGSTOP) {
                curr->state = PROCESS_SLEEPING;
                scheduler_remove(curr);
                scheduler_request_reschedule();
                return;
            }

            signal_handler_t handler = (signal_handler_t)curr->signal_handlers[sig];
            if (handler == SIG_IGN || (handler == SIG_DFL && (sig == SIGCHLD || sig == SIGCONT))) {
                continue;
            }

            if (handler == SIG_DFL) {
                /* Default fatal termination */
                serial_write("[SEC] PID "); serial_write_dec(curr->pid);
                serial_write(" terminated by signal "); serial_write_dec(sig);
                serial_write("\n");
                process_exit(128 + sig);
                return;
            } else {
                /* Custom handler delivery: Push return RIP and set entry like a CALL instruction */
                uint64_t *sp = (uint64_t *)frame->rsp;
                sp -= 1;
                sp[0] = frame->rip;
                frame->rsp = (uint64_t)sp;
                frame->rip = (uint64_t)handler;
                frame->rdi = (uint64_t)sig;
                return;
            }
        }
    }
}
