/* =============================================================================
 * Nyota OS — Spinlock & Atomic Synchronization Primitives (Phase 11)
 * Provides SMP-safe busy-waiting locks, interrupt-safe locks, and atomics.
 * =========================================================================== */

#ifndef NYOTA_SPINLOCK_H
#define NYOTA_SPINLOCK_H

#include "types.h"

typedef struct {
    volatile uint32_t lock;
} spinlock_t;

#define SPINLOCK_INIT { 0 }

static inline void spinlock_init(spinlock_t *sl) {
    if (sl) sl->lock = 0;
}

static inline void spinlock_acquire(spinlock_t *sl) {
    if (!sl) return;
    while (__atomic_test_and_set(&sl->lock, __ATOMIC_ACQUIRE)) {
        while (sl->lock) {
            __asm__ volatile ("pause");
        }
    }
}

static inline bool spinlock_try_acquire(spinlock_t *sl) {
    if (!sl) return false;
    return !__atomic_test_and_set(&sl->lock, __ATOMIC_ACQUIRE);
}

static inline void spinlock_release(spinlock_t *sl) {
    if (!sl) return;
    __atomic_clear(&sl->lock, __ATOMIC_RELEASE);
}

static inline uint64_t spinlock_acquire_irqsave(spinlock_t *sl) {
    uint64_t rflags;
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(rflags) : : "memory");
    spinlock_acquire(sl);
    return rflags;
}

static inline void spinlock_release_irqrestore(spinlock_t *sl, uint64_t rflags) {
    spinlock_release(sl);
    if (rflags & 0x200) {
        __asm__ volatile ("sti" : : : "memory");
    }
}

/* ── Atomic Operations ────────────────────────────────────────────────────── */

static inline uint32_t atomic_inc(volatile uint32_t *ptr) {
    return __atomic_add_fetch(ptr, 1, __ATOMIC_SEQ_CST);
}

static inline uint32_t atomic_dec(volatile uint32_t *ptr) {
    return __atomic_sub_fetch(ptr, 1, __ATOMIC_SEQ_CST);
}

static inline uint32_t atomic_add(volatile uint32_t *ptr, uint32_t val) {
    return __atomic_add_fetch(ptr, val, __ATOMIC_SEQ_CST);
}

static inline uint32_t atomic_sub(volatile uint32_t *ptr, uint32_t val) {
    return __atomic_sub_fetch(ptr, val, __ATOMIC_SEQ_CST);
}

static inline uint32_t atomic_load(volatile uint32_t *ptr) {
    return __atomic_load_n(ptr, __ATOMIC_SEQ_CST);
}

static inline void atomic_store(volatile uint32_t *ptr, uint32_t val) {
    __atomic_store_n(ptr, val, __ATOMIC_SEQ_CST);
}

static inline bool atomic_cas(volatile uint32_t *ptr, uint32_t *expected, uint32_t desired) {
    return __atomic_compare_exchange_n(ptr, expected, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline uint64_t atomic_inc64(volatile uint64_t *ptr) {
    return __atomic_add_fetch(ptr, 1, __ATOMIC_SEQ_CST);
}

static inline uint64_t atomic_load64(volatile uint64_t *ptr) {
    return __atomic_load_n(ptr, __ATOMIC_SEQ_CST);
}

static inline void atomic_store64(volatile uint64_t *ptr, uint64_t val) {
    __atomic_store_n(ptr, val, __ATOMIC_SEQ_CST);
}

#endif /* NYOTA_SPINLOCK_H */
