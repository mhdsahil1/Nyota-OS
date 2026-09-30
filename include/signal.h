/* =============================================================================
 * Nyota OS — POSIX Signals Interface (Phase 8)
 * Signal constants, sigset_t, signal actions, and kernel prototypes.
 * =========================================================================== */

#ifndef NYOTA_SIGNAL_H
#define NYOTA_SIGNAL_H

#include "types.h"
#include "interrupts.h"

/* Standard Signals */
#define SIGHUP          1
#define SIGINT          2
#define SIGQUIT         3
#define SIGILL          4
#define SIGTRAP         5
#define SIGABRT         6
#define SIGFPE          8
#define SIGKILL         9
#define SIGSEGV         11
#define SIGPIPE         13
#define SIGALRM         14
#define SIGTERM         15
#define SIGCHLD         17
#define SIGCONT         18
#define SIGSTOP         19
#define SIGTSTP         20

#define NSIG            32

#define SIG_DFL         ((void (*)(int))0)
#define SIG_IGN         ((void (*)(int))1)

typedef uint32_t sigset_t;
typedef void (*signal_handler_t)(int);

struct process;

void signal_init_process(struct process *proc);
int signal_send(struct process *target, int sig);
int signal_send_pid(uint32_t pid, int sig);
void signal_send_pgrp(uint32_t pgrp, int sig);
void signal_check_and_deliver(interrupt_frame_t *frame);

#endif /* NYOTA_SIGNAL_H */
