/* =============================================================================
 * Nyota OS — Kernel Entropy & Randomness Subsystem (Phase 8)
 * Generates pseudo-random entropy for ASLR and the SYS_GETRANDOM syscall.
 * =========================================================================== */

#include "security/random.h"
#include "syscall.h"
#include "timer.h"
#include "cpu.h"
#include "memory.h"

static uint64_t entropy_state = 0x853C49E6748FEA9BULL;

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

void random_init(void) {
    uint64_t seed = rdtsc();
    seed ^= ((uint64_t)timer_uptime_ms() << 32);
    seed ^= 0x9E3779B97F4A7C15ULL;
    entropy_state = seed;
}

void random_add_entropy(uint64_t val) {
    entropy_state ^= val;
    /* SplitMix64 step */
    uint64_t z = (entropy_state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    entropy_state = z ^ (z >> 31);
}

uint64_t kernel_random(void) {
    random_add_entropy(rdtsc());
    uint64_t z = (entropy_state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

int64_t kernel_getrandom(void *buf, size_t len, unsigned int flags) {
    (void)flags;
    if (!buf && len > 0) return -SYS_ERR_EFAULT;
    if (len > 4096) len = 4096; /* Cap request size for safety */

    if (!user_validate_pointer(buf, len, true)) {
        return -SYS_ERR_EFAULT;
    }

    uint8_t *dst = (uint8_t *)buf;
    size_t written = 0;

    while (written < len) {
        uint64_t r = kernel_random();
        size_t chunk = len - written;
        if (chunk > sizeof(uint64_t)) chunk = sizeof(uint64_t);
        memcpy(dst + written, &r, chunk);
        written += chunk;
    }

    return (int64_t)written;
}
