/* =============================================================================
 * Nyota OS — Kernel Entropy & Randomness Subsystem (Phase 8)
 * Random number generation for ASLR, security salts, and SYS_GETRANDOM.
 * =========================================================================== */

#ifndef NYOTA_SECURITY_RANDOM_H
#define NYOTA_SECURITY_RANDOM_H

#include "types.h"

void random_init(void);
uint64_t kernel_random(void);
void random_add_entropy(uint64_t val);
int64_t kernel_getrandom(void *buf, size_t len, unsigned int flags);

#endif /* NYOTA_SECURITY_RANDOM_H */
