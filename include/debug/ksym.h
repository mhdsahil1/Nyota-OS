/* =============================================================================
 * Nyota OS — Kernel Symbol Table & Stack Unwinder
 * Symbol lookup, backtrace walking, and crash diagnostics.
 * =========================================================================== */

#ifndef NYOTA_DEBUG_KSYM_H
#define NYOTA_DEBUG_KSYM_H

#include "types.h"

typedef struct {
    uint64_t addr;
    const char *name;
} ksym_entry_t;

void ksym_init(void);
const char *ksym_lookup(uint64_t addr, uint64_t *offset);
void stack_trace_dump(uint64_t rbp, uint64_t rip);
void stack_top_dump(uint64_t rsp, size_t count);

#endif /* NYOTA_DEBUG_KSYM_H */
