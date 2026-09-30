/* =============================================================================
 * Nyota OS — Unified Input Subsystem Implementation
 * Ring buffered event queue, scheduler wait queues, and non-busy event polling.
 * =========================================================================== */

#include "drivers/input.h"
#include "scheduler.h"
#include "process.h"
#include "memory.h"
#include "time/clock.h"
#include "serial.h"

static input_event_t event_queue[INPUT_EVENT_QUEUE_SIZE];
static volatile uint32_t q_head = 0;
static volatile uint32_t q_tail = 0;
static volatile uint32_t q_count = 0;

static process_t *input_waiters = NULL;

void input_init(void) {
    memset(event_queue, 0, sizeof(event_queue));
    q_head = 0;
    q_tail = 0;
    q_count = 0;
    input_waiters = NULL;
}

void input_post_event(const input_event_t *ev) {
    if (!ev) return;

    /* Push event if queue has room */
    if (q_count < INPUT_EVENT_QUEUE_SIZE) {
        input_event_t *slot = &event_queue[q_tail];
        *slot = *ev;
        if (slot->timestamp == 0) {
            slot->timestamp = clock_get_monotonic_ms();
        }
        q_tail = (q_tail + 1) % INPUT_EVENT_QUEUE_SIZE;
        q_count++;
    }

    /* Wake up any sleeping process waiting for input */
    if (input_waiters) {
        process_t *waiter = input_waiters;
        input_waiters = NULL;
        scheduler_wake(waiter);
    }
}

bool input_get_event(input_event_t *out_ev, bool blocking) {
    if (!out_ev) return false;

    process_t *curr = process_get_current();

    while (q_count == 0) {
        if (!blocking || !curr) {
            return false;
        }

        /* Check for pending signals before sleeping */
        if (curr->pending_signals & ~curr->blocked_signals) {
            return false;
        }

        input_waiters = curr;
        curr->state = PROCESS_SLEEPING;
        scheduler_remove(curr);
        scheduler_request_reschedule();

        /* Sleep cleanly until woken by input_post_event or signal */
        while (curr->state == PROCESS_SLEEPING && q_count == 0) {
            __asm__ volatile ("sti; hlt");
        }

        input_waiters = NULL;
        curr->state = PROCESS_RUNNING;

        if (curr->pending_signals & ~curr->blocked_signals) {
            return false;
        }
    }

    /* Pop event from ring buffer */
    *out_ev = event_queue[q_head];
    q_head = (q_head + 1) % INPUT_EVENT_QUEUE_SIZE;
    q_count--;

    return true;
}
