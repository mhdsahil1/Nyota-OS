/* =============================================================================
 * Nyota OS — Preemptive Round-Robin Scheduler
 * Manages ready/sleep queues, time slices, context switching, and idle task.
 * =========================================================================== */

#ifndef NYOTA_SCHEDULER_H
#define NYOTA_SCHEDULER_H

#include "types.h"
#include "process.h"
#include "interrupts.h"

/* Default time slice: 5 timer ticks at 100 Hz = 50 ms */
#define SCHEDULER_DEFAULT_TIME_SLICE    5

/* Scheduler Lifecycle & Core Operations */
void scheduler_init(void);
void scheduler_start(void);
bool scheduler_is_active(void);

void scheduler_add(process_t *proc);
void scheduler_remove(process_t *proc);
process_t *scheduler_next(void);

process_t *scheduler_get_current(void);
process_t *scheduler_get_idle(void);

/* Called by timer ISR every tick */
void scheduler_on_timer_tick(void);

/* Invoked by central interrupt dispatcher */
interrupt_frame_t *scheduler_schedule(interrupt_frame_t *frame);

/* Cooperative scheduling requests */
void scheduler_yield(void);
void scheduler_sleep(uint64_t ms);
void scheduler_request_reschedule(void);

/* Diagnostic inspection */
void scheduler_print_stats(void);

#endif /* NYOTA_SCHEDULER_H */
