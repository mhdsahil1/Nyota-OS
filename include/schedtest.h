/* =============================================================================
 * Nyota OS — Scheduler & Multitasking Validation Suite (Phase 5)
 * Automated tests for process table, queues, preemption, sleep/wake, and isolation.
 * =========================================================================== */

#ifndef NYOTA_SCHEDTEST_H
#define NYOTA_SCHEDTEST_H

#include "types.h"

void schedtest_run_all(void);
void schedtest_spawn_triplet(void);
void schedtest_trigger_isolation_violation(void);

#endif /* NYOTA_SCHEDTEST_H */
